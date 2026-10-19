#include <switch.h>
#include <SDL.h>
#include <SDL_ttf.h>
#include <curl/curl.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define STBI_NO_STDIO
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#define STBI_MAX_DIMENSIONS 2048
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define SCREEN_W 1280
#define SCREEN_H 720
#define MAX_CHATS 8
#define MAX_MODELS 64
#define MAX_MESSAGES 12
#define MESSAGE_SIZE 8192
#define HTTP_SIZE (256 * 1024)
#define THEME_COUNT 6
#define MAX_IMAGE_CACHE 4
#define MAX_IMAGE_BYTES (5 * 1024 * 1024)
#define MAX_IMAGE_PIXELS (2 * 1024 * 1024)
#define IMAGE_PLACEHOLDER_H 150
#define CLIENT_USER_AGENT "Switch-AI-Chat/0.9.4"

typedef struct {
    char role[12];
    char text[MESSAGE_SIZE];
} Message;

typedef struct {
    char title[64];
    Message messages[MAX_MESSAGES];
    int message_count;
    char created_at[24];
    int scroll_y;
} Chat;

typedef struct {
    char base_url[512];
    char api_key[512];
    char model[160];
    int theme_id;
    int web_search;
} AppConfig;

typedef enum { VIEW_CHAT, VIEW_SETTINGS, VIEW_MODELS } View;

typedef struct {
    Uint8 bg[3], panel[3], card[3], selected[3];
    SDL_Color text, muted, accent, danger;
} Theme;

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} HttpBuffer;

typedef struct {
    SDL_Texture *texture;
    unsigned hash;
    int width;
    int height;
    int theme_id;
    int user;
} BubbleCache;

typedef enum { IMAGE_EMPTY, IMAGE_LOADING, IMAGE_READY, IMAGE_FAILED } ImageState;

typedef struct {
    char url[1024];
    unsigned char *data;
    size_t length;
    size_t capacity;
    SDL_Texture *texture;
    CURL *easy;
    ImageState state;
    int width;
    int height;
    int too_large;
    unsigned last_used;
    char error[96];
} ImageEntry;

static const char *CONFIG_DIR = "sdmc:/switch/SwitchAIChat";
static const char *CONFIG_PATH = "sdmc:/switch/SwitchAIChat/config.ini";
static const char *SESSIONS_PATH = "sdmc:/switch/SwitchAIChat/sessions.bin";
static Chat chats[MAX_CHATS];
static int chat_count;
static int active_chat=-1;
static int confirm_delete;
static AppConfig config;
static char models[MAX_MODELS][160];
static int model_count;
static int selected_model;
static int setting_row;
static int sidebar_collapsed;
static int touch_last_y;
static float scroll_velocity;
static char draft_text[MESSAGE_SIZE];
static volatile int is_generating;
static volatile int cancel_requested;
static unsigned height_cache_hash[MAX_CHATS][MAX_MESSAGES];
static int height_cache_width[MAX_CHATS][MAX_MESSAGES];
static int height_cache_value[MAX_CHATS][MAX_MESSAGES];
static BubbleCache bubble_cache[MAX_CHATS][MAX_MESSAGES];
static int bubble_cache_chat=-1;
static ImageEntry image_cache[MAX_IMAGE_CACHE];
static CURLM *image_multi;
static unsigned image_frame;
static unsigned image_layout_revision=1;
static View current_view = VIEW_CHAT;
static char status_text[256] = "ZR 打开设置";
static TTF_Font *live_font;
static TTF_Font *live_symbol;
static TTF_Font *live_emoji;

static void draw_chat(SDL_Renderer *r, TTF_Font *font, TTF_Font *small, Theme t);
static void destroy_bubble_cache(void);
static void cleanup_image_cache(void);

void userAppInit(void) {
    Result rc = plInitialize(PlServiceType_User);
    if (R_FAILED(rc)) diagAbortWithResult(rc);
}

void userAppExit(void) {
    plExit();
}

static const char *theme_name(void) {
    static const char *names[THEME_COUNT] = {"深海夜色","明亮纸张","海洋蓝绿","暖阳珊瑚","紫藤星云","薄荷森林"};
    return names[config.theme_id>=0&&config.theme_id<THEME_COUNT?config.theme_id:0];
}

static Theme get_theme(void) {
    static const Theme themes[] = {
        {{14,20,32},{23,34,53},{31,45,65},{48,82,125},{235,240,250,255},{160,175,195,255},{92,170,255,255},{255,120,120,255}},
        {{238,242,248},{250,252,255},{222,230,241},{188,214,247},{32,43,59,255},{92,108,128,255},{25,105,210,255},{190,52,52,255}},
        {{7,31,38},{11,52,61},{15,67,78},{22,111,122},{224,250,247,255},{133,191,190,255},{53,220,199,255},{255,127,121,255}},
        {{42,24,31},{68,36,43},{88,49,52},{142,70,66},{255,242,225,255},{218,174,151,255},{255,164,92,255},{255,105,97,255}},
        {{24,17,39},{36,25,56},{51,36,74},{92,65,132},{247,239,255,255},{184,160,205,255},{197,121,255,255},{255,113,145,255}},
        {{232,244,238},{245,251,247},{213,233,221},{174,218,191},{31,62,47,255},{86,116,99,255},{30,151,91,255},{196,60,72,255}}
    };
    int id=config.theme_id;if(id<0||id>=THEME_COUNT)id=0;return themes[id];
}

static void fill(SDL_Renderer *renderer, int x, int y, int w, int h, const Uint8 color[3]) {
    SDL_SetRenderDrawColor(renderer, color[0], color[1], color[2], 255);
    SDL_Rect rect = {x, y, w, h};
    SDL_RenderFillRect(renderer, &rect);
}

static void draw_text(SDL_Renderer *renderer, TTF_Font *font, const char *value,
                      int x, int y, SDL_Color color) {
    if (!value || !value[0]) return;
    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, value, color);
    if (!surface) return;
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture) {
        SDL_Rect target = {x, y, surface->w, surface->h};
        SDL_RenderCopy(renderer, texture, NULL, &target);
        SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(surface);
}

static int font_covers_text(TTF_Font *font,const char *value) {
    const unsigned char *p=(const unsigned char*)value;
    while(*p){Uint32 cp=0;int bytes=1;if(*p<0x80)cp=*p;else if((*p&0xE0)==0xC0&&p[1]){cp=((Uint32)(p[0]&31)<<6)|(p[1]&63);bytes=2;}else if((*p&0xF0)==0xE0&&p[1]&&p[2]){cp=((Uint32)(p[0]&15)<<12)|((Uint32)(p[1]&63)<<6)|(p[2]&63);bytes=3;}else if((*p&0xF8)==0xF0&&p[1]&&p[2]&&p[3]){cp=((Uint32)(p[0]&7)<<18)|((Uint32)(p[1]&63)<<12)|((Uint32)(p[2]&63)<<6)|(p[3]&63);bytes=4;}else cp=0xFFFD;p+=bytes;if(cp=='\n'||cp=='\r'||cp=='\t'||cp==' ')continue;if(!TTF_GlyphIsProvided32(font,cp))return 0;}
    return 1;
}

static int draw_wrapped(SDL_Renderer *renderer, TTF_Font *font, const char *value,
                        int x, int y, int width, SDL_Color color) {
    if (!value || !value[0]) return 0;
    if(font_covers_text(font,value)){
        SDL_Surface *surface=TTF_RenderUTF8_Blended_Wrapped(font,value,color,(Uint32)width);if(!surface)return 0;int height=surface->h;
        if(renderer){SDL_Texture *texture=SDL_CreateTextureFromSurface(renderer,surface);if(texture){SDL_Rect target={x,y,surface->w,surface->h};SDL_RenderCopy(renderer,texture,NULL,&target);SDL_DestroyTexture(texture);}}SDL_FreeSurface(surface);return height;
    }
    int cx=x,cy=y,line_h=TTF_FontHeight(font)+5,max_y=cy+line_h;
    const unsigned char *p=(const unsigned char*)value;
    while(*p){
        Uint32 cp=0;int bytes=1;
        if(*p<0x80)cp=*p;
        else if((*p&0xE0)==0xC0&&p[1]){cp=((Uint32)(p[0]&31)<<6)|(p[1]&63);bytes=2;}
        else if((*p&0xF0)==0xE0&&p[1]&&p[2]){cp=((Uint32)(p[0]&15)<<12)|((Uint32)(p[1]&63)<<6)|(p[2]&63);bytes=3;}
        else if((*p&0xF8)==0xF0&&p[1]&&p[2]&&p[3]){cp=((Uint32)(p[0]&7)<<18)|((Uint32)(p[1]&63)<<12)|((Uint32)(p[2]&63)<<6)|(p[3]&63);bytes=4;}
        else cp=0xFFFD;
        p+=bytes;
        if(cp=='\r')continue;
        if(cp=='\n'){cx=x;cy+=line_h;max_y=cy+line_h;continue;}
        if(cp==0xFE0F||cp==0x200D)continue;
        TTF_Font *use=font;
        if(!TTF_GlyphIsProvided32(font,cp)){
            if(cp>=0x1F000&&live_emoji&&TTF_GlyphIsProvided32(live_emoji,cp))use=live_emoji;
            else if(live_symbol&&TTF_GlyphIsProvided32(live_symbol,cp))use=live_symbol;
            else if(live_emoji&&TTF_GlyphIsProvided32(live_emoji,cp))use=live_emoji;
            else {cp='?';use=font;}
        }
        int minx=0,maxx=0,miny=0,maxy=0,advance=0;
        if(TTF_GlyphMetrics32(use,cp,&minx,&maxx,&miny,&maxy,&advance)!=0)advance=line_h/2;
        if(cx>x&&cx+advance>x+width){cx=x;cy+=line_h;max_y=cy+line_h;}
        if(renderer&&cp!=' '){SDL_Surface *surface=TTF_RenderGlyph32_Blended(use,cp,color);if(surface){SDL_Texture *texture=SDL_CreateTextureFromSurface(renderer,surface);if(texture){SDL_Rect target={cx,cy+(line_h-surface->h)/2,surface->w,surface->h};SDL_RenderCopy(renderer,texture,NULL,&target);SDL_DestroyTexture(texture);}SDL_FreeSurface(surface);}}
        cx+=advance;if(cy+line_h>max_y)max_y=cy+line_h;
    }
    return max_y-y;
}

static void fill_rounded(SDL_Renderer *r,int x,int y,int w,int h,int radius,const Uint8 color[3]) {
    if(radius<1){fill(r,x,y,w,h,color);return;}if(radius*2>h)radius=h/2;if(radius*2>w)radius=w/2;
    SDL_SetRenderDrawColor(r,color[0],color[1],color[2],255);
    SDL_Rect center={x+radius,y,w-radius*2,h};SDL_RenderFillRect(r,&center);
    SDL_Rect middle={x,y+radius,w,h-radius*2};SDL_RenderFillRect(r,&middle);
    for(int dy=0;dy<radius;dy++){int yy=radius-dy-1;int dx=radius-(int)sqrt((double)(radius*radius-yy*yy));SDL_RenderDrawLine(r,x+dx,y+dy,x+w-dx-1,y+dy);SDL_RenderDrawLine(r,x+dx,y+h-dy-1,x+w-dx-1,y+h-dy-1);}
}

static void trim_line(char *value) {
    size_t n = strlen(value);
    while (n && (value[n - 1] == '\r' || value[n - 1] == '\n')) value[--n] = 0;
}

static void load_config(void) {
    memset(&config, 0, sizeof(config));
    strcpy(config.base_url, "https://api.openai.com/v1");
    config.theme_id = 0;
    FILE *file = fopen(CONFIG_PATH, "r");
    if (!file) return;
    char line[1100];
    while (fgets(line, sizeof(line), file)) {
        trim_line(line);
        char *equals = strchr(line, '=');
        if (!equals) continue;
        *equals++ = 0;
        if (!strcmp(line, "base_url")) snprintf(config.base_url, sizeof(config.base_url), "%s", equals);
        else if (!strcmp(line, "api_key")) snprintf(config.api_key, sizeof(config.api_key), "%s", equals);
        else if (!strcmp(line, "model")) snprintf(config.model, sizeof(config.model), "%s", equals);
        else if (!strcmp(line, "theme")) config.theme_id = atoi(equals);
        else if (!strcmp(line, "web_search")) config.web_search = atoi(equals) != 0;
        else if (!strcmp(line, "dark_mode")) config.theme_id = atoi(equals) ? 0 : 1;
    }
    fclose(file);
}

static int save_config(void) {
    mkdir("sdmc:/switch", 0777);
    mkdir(CONFIG_DIR, 0777);
    FILE *file = fopen(CONFIG_PATH, "w");
    if (!file) return 0;
    fprintf(file, "base_url=%s\napi_key=%s\nmodel=%s\ntheme=%d\nweb_search=%d\n",
            config.base_url, config.api_key, config.model, config.theme_id,
            config.web_search ? 1 : 0);
    fclose(file);
    return 1;
}

static int keyboard_input(char *target, size_t size, const char *header, int password) {
    SwkbdConfig keyboard;
    if (R_FAILED(swkbdCreate(&keyboard, 0))) return 0;
    if (password) swkbdConfigMakePresetPassword(&keyboard);
    else swkbdConfigMakePresetDefault(&keyboard);
    if (!password && !strcmp(header, "输入 Base URL")) swkbdConfigSetType(&keyboard, SwkbdType_QWERTY);
    else if (!password) swkbdConfigSetType(&keyboard, SwkbdType_ZhHans);
    swkbdConfigSetStringLenMax(&keyboard, (u32)(size - 1));
    swkbdConfigSetHeaderText(&keyboard, header);
    swkbdConfigSetInitialText(&keyboard, target);
    char *buffer = calloc(1, size);
    if (!buffer) { swkbdClose(&keyboard); return 0; }
    Result rc = swkbdShow(&keyboard, buffer, size);
    swkbdClose(&keyboard);
    if (R_SUCCEEDED(rc) && buffer[0]) {
        snprintf(target, size, "%s", buffer);
        free(buffer);
        return 1;
    }
    free(buffer);
    return 0;
}

typedef struct {
    char role[12];
    char text[2048];
} LegacyMessage;

typedef struct {
    char title[64];
    LegacyMessage messages[MAX_MESSAGES];
    int message_count;
} OldChat;

typedef struct {
    char magic[8];
    int version;
    int count;
    int active;
    OldChat saved_chats[MAX_CHATS];
} OldSessionStore;

typedef struct {
    char title[64];
    LegacyMessage messages[MAX_MESSAGES];
    int message_count;
    char created_at[24];
    int scroll_y;
} V2Chat;

typedef struct {
    char magic[8];
    int version;
    int count;
    int active;
    V2Chat saved_chats[MAX_CHATS];
} V2SessionStore;

typedef struct {
    char magic[8];
    int version;
    int count;
    int active;
    Chat saved_chats[MAX_CHATS];
} SessionStore;

static void save_sessions(void) {
    mkdir("sdmc:/switch", 0777);
    mkdir(CONFIG_DIR, 0777);
    FILE *file = fopen(SESSIONS_PATH, "wb");
    if (!file) return;
    SessionStore *store=calloc(1,sizeof(*store));
    if(!store){fclose(file);return;}
    memcpy(store->magic, "SAICHAT", 7);
    store->version = 3;
    store->count = chat_count;
    store->active = active_chat;
    memcpy(store->saved_chats, chats, sizeof(chats));
    fwrite(store, sizeof(*store), 1, file);free(store);
    fclose(file);
}

static int load_sessions(void) {
    FILE *file = fopen(SESSIONS_PATH, "rb");
    if (!file) return 0;
    char magic[8];int version=0;
    if(fread(magic,sizeof(magic),1,file)!=1||fread(&version,sizeof(version),1,file)!=1){fclose(file);return 0;}
    rewind(file);
    if(!memcmp(magic,"SAICHAT",7)&&version==1){
        OldSessionStore old;
        int ok=fread(&old,sizeof(old),1,file)==1;fclose(file);
        if(!ok||old.count<1||old.count>MAX_CHATS)return 0;
        memset(chats,0,sizeof(chats));
        for(int i=0;i<old.count;i++){snprintf(chats[i].title,sizeof(chats[i].title),"%s",old.saved_chats[i].title);chats[i].message_count=old.saved_chats[i].message_count;for(int j=0;j<chats[i].message_count;j++){snprintf(chats[i].messages[j].role,sizeof(chats[i].messages[j].role),"%s",old.saved_chats[i].messages[j].role);snprintf(chats[i].messages[j].text,sizeof(chats[i].messages[j].text),"%s",old.saved_chats[i].messages[j].text);}snprintf(chats[i].created_at,sizeof(chats[i].created_at),"旧会话");}
        chat_count=old.count;active_chat=old.active>=0&&old.active<chat_count?old.active:0;save_sessions();return 1;
    }
    if(!memcmp(magic,"SAICHAT",7)&&version==2){
        V2SessionStore *old=malloc(sizeof(*old));int ok=old&&fread(old,sizeof(*old),1,file)==1;fclose(file);
        if(!ok||old->count<1||old->count>MAX_CHATS){free(old);return 0;}memset(chats,0,sizeof(chats));
        for(int i=0;i<old->count;i++){snprintf(chats[i].title,sizeof(chats[i].title),"%s",old->saved_chats[i].title);snprintf(chats[i].created_at,sizeof(chats[i].created_at),"%s",old->saved_chats[i].created_at);chats[i].scroll_y=old->saved_chats[i].scroll_y;chats[i].message_count=old->saved_chats[i].message_count;for(int j=0;j<chats[i].message_count;j++){snprintf(chats[i].messages[j].role,sizeof(chats[i].messages[j].role),"%s",old->saved_chats[i].messages[j].role);snprintf(chats[i].messages[j].text,sizeof(chats[i].messages[j].text),"%s",old->saved_chats[i].messages[j].text);}}
        chat_count=old->count;active_chat=old->active>=0&&old->active<chat_count?old->active:0;free(old);save_sessions();return 1;
    }
    SessionStore *store=malloc(sizeof(*store));
    int ok = store&&fread(store, sizeof(*store), 1, file) == 1;
    fclose(file);
    if (!ok || memcmp(store->magic, "SAICHAT", 7) || store->version != 3 ||
        store->count < 0 || store->count > MAX_CHATS){free(store);return 0;}
    memcpy(chats, store->saved_chats, sizeof(chats));
    chat_count = store->count;
    active_chat = chat_count>0?(store->active >= 0 && store->active < chat_count ? store->active : 0):-1;free(store);
    return 1;
}

static void add_chat(void) {
    if (chat_count >= MAX_CHATS){snprintf(status_text,sizeof(status_text),"最多只能保留 %d 个会话",MAX_CHATS);return;}
    Chat *chat = &chats[chat_count];
    memset(chat, 0, sizeof(*chat));
    snprintf(chat->title, sizeof(chat->title), "新建对话 %d", chat_count + 1);
    time_t now=time(NULL);struct tm *local=localtime(&now);
    if(local)strftime(chat->created_at,sizeof(chat->created_at),"%Y-%m-%d %H:%M",local);
    else snprintf(chat->created_at,sizeof(chat->created_at),"刚刚创建");
    active_chat = chat_count++;
    save_sessions();
}

static void rename_chat(void) {
    if(chat_count<=0||active_chat<0)return;
    char title[64];snprintf(title,sizeof(title),"%s",chats[active_chat].title);
    if(keyboard_input(title,sizeof(title),"修改会话标题",0)){snprintf(chats[active_chat].title,sizeof(chats[active_chat].title),"%s",title);save_sessions();snprintf(status_text,sizeof(status_text),"会话标题已修改");}
}

static void delete_chat(void) {
    if (chat_count <= 0 || active_chat < 0) return;
    destroy_bubble_cache();
    memmove(&chats[active_chat], &chats[active_chat + 1],
            (size_t)(chat_count - active_chat - 1) * sizeof(Chat));
    chat_count--;
    memset(&chats[chat_count],0,sizeof(Chat));
    if(chat_count==0)active_chat=-1;
    else if (active_chat >= chat_count) active_chat = chat_count - 1;
    bubble_cache_chat=active_chat;
    snprintf(status_text,sizeof(status_text),chat_count?"会话已删除":"全部会话已删除，可新建会话继续");
    save_sessions();
}

static size_t http_write(void *contents, size_t size, size_t count, void *user) {
    size_t incoming = size * count;
    HttpBuffer *buffer = user;
    if (buffer->length + incoming + 1 > buffer->capacity) return 0;
    memcpy(buffer->data + buffer->length, contents, incoming);
    buffer->length += incoming;
    buffer->data[buffer->length] = 0;
    return incoming;
}

static void endpoint_url(char *output, size_t size, const char *path) {
    size_t n = strlen(config.base_url);
    while (n && config.base_url[n - 1] == '/') n--;
    snprintf(output, size, "%.*s/%s", (int)n, config.base_url, path);
}

static int http_request(const char *url, const char *post_data, char *response,
                        size_t response_size, char *error, size_t error_size) {
    CURL *curl = curl_easy_init();
    if (!curl) { snprintf(error, error_size, "无法初始化网络请求"); return 0; }
    HttpBuffer buffer = {response, 0, response_size};
    response[0] = 0;
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "Content-Type: application/json");
    char authorization[640];
    if (config.api_key[0]) {
        snprintf(authorization, sizeof(authorization), "Authorization: Bearer %s", config.api_key);
        headers = curl_slist_append(headers, authorization);
    }
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, CLIENT_USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 90L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    if (post_data) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
    }
    CURLcode result = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (result != CURLE_OK) {
        if (result == CURLE_COULDNT_RESOLVE_HOST)
            snprintf(error, error_size, "无法解析域名，请检查 Base URL（例如域名是否拼错）");
        else if (result == CURLE_COULDNT_CONNECT)
            snprintf(error, error_size, "无法连接服务器，请检查网络和 Base URL");
        else if (result == CURLE_OPERATION_TIMEDOUT)
            snprintf(error, error_size, "请求超时，请检查网络后重试");
        else if (result == CURLE_SSL_CONNECT_ERROR || result == CURLE_PEER_FAILED_VERIFICATION)
            snprintf(error, error_size, "TLS 证书验证失败，请检查地址和主机时间");
        else
            snprintf(error, error_size, "网络错误：%s", curl_easy_strerror(result));
        return 0;
    }
    if (status < 200 || status >= 300) {
        snprintf(error, error_size, "接口返回 HTTP %ld：%.120s", status, response);
        return 0;
    }
    return 1;
}

static int ends_with_ignore_case(const char *value,const char *suffix) {
    size_t a=strlen(value),b=strlen(suffix);if(a<b)return 0;
    for(size_t i=0;i<b;i++)if(tolower((unsigned char)value[a-b+i])!=tolower((unsigned char)suffix[i]))return 0;
    return 1;
}

static int image_url_is_safe(const char *url,char *error,size_t error_size) {
    const char *authority=NULL;
    if(!strncmp(url,"https://",8))authority=url+8;
    else if(!strncmp(url,"http://",7))authority=url+7;
    else {snprintf(error,error_size,"仅支持 HTTP/HTTPS 图片");return 0;}
    const char *end=authority+strcspn(authority,"/?#");
    if(end==authority||(size_t)(end-authority)>=256||memchr(authority,'@',(size_t)(end-authority))){snprintf(error,error_size,"图片地址无效");return 0;}
    char host[256];size_t length=(size_t)(end-authority);memcpy(host,authority,length);host[length]=0;
    if(host[0]=='['){char *close=strchr(host,']');if(!close){snprintf(error,error_size,"图片主机无效");return 0;}close[1]=0;}
    else {char *port=strrchr(host,':');if(port)*port=0;}
    for(char *p=host;*p;p++)*p=(char)tolower((unsigned char)*p);
    if(!strcmp(host,"localhost")||ends_with_ignore_case(host,".local")||!strncmp(host,"127.",4)||!strncmp(host,"10.",3)||!strncmp(host,"192.168.",8)||!strncmp(host,"169.254.",8)||!strncmp(host,"0.",2)||!strcmp(host,"[::1]")||!strncmp(host,"[fc",3)||!strncmp(host,"[fd",3)||!strncmp(host,"[fe80",5)){
        snprintf(error,error_size,"已阻止本地网络图片地址");return 0;
    }
    return 1;
}

static void invalidate_message_layout(void) {
    memset(height_cache_hash,0,sizeof(height_cache_hash));
    memset(height_cache_width,0,sizeof(height_cache_width));
    image_layout_revision++;
    if(!image_layout_revision)image_layout_revision=1;
    destroy_bubble_cache();
}

static void reset_image_entry(ImageEntry *entry) {
    if(entry->easy&&image_multi)curl_multi_remove_handle(image_multi,entry->easy);
    if(entry->easy)curl_easy_cleanup(entry->easy);
    if(entry->texture)SDL_DestroyTexture(entry->texture);
    free(entry->data);memset(entry,0,sizeof(*entry));
}

static size_t image_write(void *contents,size_t size,size_t count,void *user) {
    ImageEntry *entry=user;size_t incoming=size*count;
    if(!incoming)return 0;
    if(entry->length+incoming>MAX_IMAGE_BYTES){entry->too_large=1;return 0;}
    size_t needed=entry->length+incoming;
    if(needed>entry->capacity){size_t capacity=entry->capacity?entry->capacity:65536;while(capacity<needed&&capacity<MAX_IMAGE_BYTES)capacity*=2;if(capacity>MAX_IMAGE_BYTES)capacity=MAX_IMAGE_BYTES;unsigned char *next=realloc(entry->data,capacity);if(!next)return 0;entry->data=next;entry->capacity=capacity;}
    memcpy(entry->data+entry->length,contents,incoming);entry->length+=incoming;return incoming;
}

static ImageEntry *find_image_entry(const char *url) {
    for(int i=0;i<MAX_IMAGE_CACHE;i++)if(image_cache[i].state!=IMAGE_EMPTY&&!strcmp(image_cache[i].url,url)){image_cache[i].last_used=image_frame;return &image_cache[i];}
    return NULL;
}

static ImageEntry *start_image_download(const char *url) {
    ImageEntry *existing=find_image_entry(url);if(existing)return existing;
    ImageEntry *entry=NULL;
    for(int i=0;i<MAX_IMAGE_CACHE;i++)if(image_cache[i].state==IMAGE_EMPTY){entry=&image_cache[i];break;}
    if(!entry){for(int i=0;i<MAX_IMAGE_CACHE;i++)if(image_cache[i].state!=IMAGE_LOADING&&(!entry||image_cache[i].last_used<entry->last_used))entry=&image_cache[i];}
    if(!entry)return NULL;
    reset_image_entry(entry);snprintf(entry->url,sizeof(entry->url),"%s",url);entry->last_used=image_frame;
    if(!image_url_is_safe(url,entry->error,sizeof(entry->error))){entry->state=IMAGE_FAILED;return entry;}
    if(!image_multi)image_multi=curl_multi_init();
    entry->easy=curl_easy_init();
    if(!image_multi||!entry->easy){snprintf(entry->error,sizeof(entry->error),"无法初始化图片下载");entry->state=IMAGE_FAILED;return entry;}
    entry->state=IMAGE_LOADING;
    curl_easy_setopt(entry->easy,CURLOPT_URL,entry->url);
    curl_easy_setopt(entry->easy,CURLOPT_WRITEFUNCTION,image_write);
    curl_easy_setopt(entry->easy,CURLOPT_WRITEDATA,entry);
    curl_easy_setopt(entry->easy,CURLOPT_PRIVATE,entry);
    curl_easy_setopt(entry->easy,CURLOPT_USERAGENT,CLIENT_USER_AGENT);
    curl_easy_setopt(entry->easy,CURLOPT_FOLLOWLOCATION,1L);
    curl_easy_setopt(entry->easy,CURLOPT_MAXREDIRS,3L);
    curl_easy_setopt(entry->easy,CURLOPT_PROTOCOLS,(long)(CURLPROTO_HTTP|CURLPROTO_HTTPS));
    curl_easy_setopt(entry->easy,CURLOPT_REDIR_PROTOCOLS,(long)(CURLPROTO_HTTP|CURLPROTO_HTTPS));
    curl_easy_setopt(entry->easy,CURLOPT_CONNECTTIMEOUT,10L);
    curl_easy_setopt(entry->easy,CURLOPT_TIMEOUT,45L);
    curl_easy_setopt(entry->easy,CURLOPT_MAXFILESIZE_LARGE,(curl_off_t)MAX_IMAGE_BYTES);
    curl_easy_setopt(entry->easy,CURLOPT_ACCEPT_ENCODING,"");
    curl_easy_setopt(entry->easy,CURLOPT_NOSIGNAL,1L);
    if(curl_multi_add_handle(image_multi,entry->easy)!=CURLM_OK){curl_easy_cleanup(entry->easy);entry->easy=NULL;entry->state=IMAGE_FAILED;snprintf(entry->error,sizeof(entry->error),"无法加入图片下载队列");}
    return entry;
}

static void finish_image_download(ImageEntry *entry,CURLcode result,SDL_Renderer *renderer) {
    long status=0;if(entry->easy)curl_easy_getinfo(entry->easy,CURLINFO_RESPONSE_CODE,&status);
    if(entry->easy&&image_multi)curl_multi_remove_handle(image_multi,entry->easy);
    if(entry->easy)curl_easy_cleanup(entry->easy);
    entry->easy=NULL;
    if(result!=CURLE_OK||status<200||status>=300){entry->state=IMAGE_FAILED;if(entry->too_large)snprintf(entry->error,sizeof(entry->error),"图片超过 5 MiB 限制");else snprintf(entry->error,sizeof(entry->error),"下载失败（HTTP %ld）",status);free(entry->data);entry->data=NULL;entry->length=entry->capacity=0;invalidate_message_layout();return;}
    int width=0,height=0,channels=0;
    if(!stbi_info_from_memory(entry->data,(int)entry->length,&width,&height,&channels)||width<1||height<1||width>2048||height>2048||(long long)width*height>MAX_IMAGE_PIXELS){entry->state=IMAGE_FAILED;snprintf(entry->error,sizeof(entry->error),"图片格式或尺寸不受支持");free(entry->data);entry->data=NULL;entry->length=entry->capacity=0;invalidate_message_layout();return;}
    unsigned char *pixels=stbi_load_from_memory(entry->data,(int)entry->length,&width,&height,&channels,STBI_rgb_alpha);
    if(!pixels){entry->state=IMAGE_FAILED;snprintf(entry->error,sizeof(entry->error),"图片解码失败");free(entry->data);entry->data=NULL;entry->length=entry->capacity=0;invalidate_message_layout();return;}
    SDL_Surface *source=SDL_CreateRGBSurfaceWithFormatFrom(pixels,width,height,32,width*4,SDL_PIXELFORMAT_RGBA32);
    SDL_Surface *scaled=NULL;int texture_width=width,texture_height=height;
    if(source&&(texture_width>960||texture_height>512)){
        double ratio_w=960.0/texture_width,ratio_h=512.0/texture_height,ratio=ratio_w<ratio_h?ratio_w:ratio_h;
        texture_width=(int)(texture_width*ratio);texture_height=(int)(texture_height*ratio);
        if(texture_width<1)texture_width=1;
        if(texture_height<1)texture_height=1;
        scaled=SDL_CreateRGBSurfaceWithFormat(0,texture_width,texture_height,32,SDL_PIXELFORMAT_RGBA32);
        if(scaled)SDL_BlitScaled(source,NULL,scaled,NULL);
    }
    SDL_Surface *texture_surface=scaled?scaled:source;
    entry->texture=texture_surface?SDL_CreateTextureFromSurface(renderer,texture_surface):NULL;
    if(scaled)SDL_FreeSurface(scaled);
    if(source)SDL_FreeSurface(source);
    stbi_image_free(pixels);free(entry->data);entry->data=NULL;entry->length=entry->capacity=0;
    if(!entry->texture){entry->state=IMAGE_FAILED;snprintf(entry->error,sizeof(entry->error),"无法创建图片纹理");}
    else {SDL_SetTextureBlendMode(entry->texture,SDL_BLENDMODE_BLEND);entry->state=IMAGE_READY;entry->width=texture_width;entry->height=texture_height;}
    invalidate_message_layout();
}

static void pump_image_downloads(SDL_Renderer *renderer) {
    image_frame++;
    if(!image_multi)return;
    int running=0;if(curl_multi_perform(image_multi,&running)!=CURLM_OK)return;
    int queued=0;CURLMsg *message;
    while((message=curl_multi_info_read(image_multi,&queued))){if(message->msg!=CURLMSG_DONE)continue;char *private_data=NULL;curl_easy_getinfo(message->easy_handle,CURLINFO_PRIVATE,&private_data);ImageEntry *entry=(ImageEntry *)private_data;if(entry)finish_image_download(entry,message->data.result,renderer);}
}

static void cleanup_image_cache(void) {
    for(int i=0;i<MAX_IMAGE_CACHE;i++)reset_image_entry(&image_cache[i]);
    if(image_multi)curl_multi_cleanup(image_multi);
    image_multi=NULL;
}

static const char *skip_space(const char *p) {
    while (*p && isspace((unsigned char)*p)) p++;
    return p;
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void append_utf8(char *out, size_t out_size, size_t *used, unsigned code) {
    unsigned char bytes[4]; int n = 0;
    if (code <= 0x7F) { bytes[0] = (unsigned char)code; n = 1; }
    else if (code <= 0x7FF) { bytes[0]=0xC0|(code>>6); bytes[1]=0x80|(code&63); n=2; }
    else if (code <= 0xFFFF) { bytes[0]=0xE0|(code>>12); bytes[1]=0x80|((code>>6)&63); bytes[2]=0x80|(code&63); n=3; }
    else { bytes[0]=0xF0|(code>>18);bytes[1]=0x80|((code>>12)&63);bytes[2]=0x80|((code>>6)&63);bytes[3]=0x80|(code&63);n=4; }
    for (int i=0; i<n && *used+1<out_size; i++) out[(*used)++] = (char)bytes[i];
}

static int json_string_after(const char *start, const char *key, char *out, size_t out_size) {
    const char *p = strstr(start, key);
    if (!p) return 0;
    p += strlen(key);
    p = skip_space(p);
    if (*p != ':') return 0;
    p = skip_space(p + 1);
    if (*p != '"') return 0;
    p++;
    size_t used = 0;
    while (*p && *p != '"' && used + 1 < out_size) {
        if (*p != '\\') { out[used++] = *p++; continue; }
        p++;
        if (*p == 'n') { out[used++]='\n'; p++; }
        else if (*p == 'r') { out[used++]='\r'; p++; }
        else if (*p == 't') { out[used++]='\t'; p++; }
        else if (*p == '"' || *p == '\\' || *p == '/') out[used++]=*p++;
        else if (*p == 'u') {
            p++;
            unsigned code=0; int ok=1;
            for(int i=0;i<4;i++){int h=hex_value(p[i]);if(h<0){ok=0;break;}code=(code<<4)|(unsigned)h;}
            if(!ok) break;
            p+=4;
            if(code>=0xD800&&code<=0xDBFF&&p[0]=='\\'&&p[1]=='u'){
                unsigned low=0;int low_ok=1;
                for(int i=0;i<4;i++){int h=hex_value(p[2+i]);if(h<0){low_ok=0;break;}low=(low<<4)|(unsigned)h;}
                if(low_ok&&low>=0xDC00&&low<=0xDFFF){code=0x10000+((code-0xD800)<<10)+(low-0xDC00);p+=6;}
            }
            append_utf8(out,out_size,&used,code);
        } else if (*p) out[used++]=*p++;
    }
    out[used] = 0;
    return *p == '"';
}

static void json_escape(const char *input, char *output, size_t size) {
    size_t used=0;
    for(const unsigned char *p=(const unsigned char*)input; *p && used+2<size; p++) {
        if(*p=='"'||*p=='\\'){output[used++]='\\';output[used++]=(char)*p;}
        else if(*p=='\n'){output[used++]='\\';output[used++]='n';}
        else if(*p>=0x20) output[used++]=(char)*p;
    }
    output[used]=0;
}

typedef struct {
    char pending[65536];
    size_t pending_length;
    Message *assistant;
    Chat *chat;
    int saw_sse;
    int saw_done;
    int saw_finish;
    int overflow;
    int responses_mode;
    char finish_reason[32];
    char server_error[160];
} StreamContext;

static void append_stream_text(StreamContext *ctx,const char *delta) {
    size_t have=strlen(ctx->assistant->text),add=strlen(delta),space=sizeof(ctx->assistant->text)-have-1;
    if(add>space){add=space;ctx->overflow=1;}
    if(add){memcpy(ctx->assistant->text+have,delta,add);ctx->assistant->text[have+add]=0;}
}

static void append_stream_image(StreamContext *ctx,const char *url) {
    if(!url[0]||strstr(ctx->assistant->text,url))return;
    char markdown[1200];snprintf(markdown,sizeof(markdown),"\n\n![模型返回图片](%s)\n",url);
    append_stream_text(ctx,markdown);
}

static void process_stream_json(StreamContext *ctx,const char *json) {
    if(!strcmp(json,"[DONE]")){ctx->saw_done=1;return;}
    if(ctx->responses_mode){
        char event_type[64];event_type[0]=0;
        json_string_after(json,"\"type\"",event_type,sizeof(event_type));
        if(!strcmp(event_type,"response.completed")){ctx->saw_done=1;return;}
        if(!strcmp(event_type,"response.incomplete")){ctx->saw_finish=1;snprintf(ctx->finish_reason,sizeof(ctx->finish_reason),"length");return;}
        if(!strcmp(event_type,"response.failed")||!strcmp(event_type,"error")){
            if(!json_string_after(json,"\"message\"",ctx->server_error,sizeof(ctx->server_error)))
                snprintf(ctx->server_error,sizeof(ctx->server_error),"Responses 请求失败");
            return;
        }
        if(!strcmp(event_type,"response.output_text.delta")){
            char delta[8192];
            if(json_string_after(json,"\"delta\"",delta,sizeof(delta))&&delta[0]){
                append_stream_text(ctx,delta);ctx->chat->scroll_y=1000000;
                snprintf(status_text,sizeof(status_text),"正在生成 · %u 字节",(unsigned)strlen(ctx->assistant->text));
            }
            return;
        }
        return;
    }
    char reason[32];
    if(json_string_after(json,"\"finish_reason\"",reason,sizeof(reason))){ctx->saw_finish=1;snprintf(ctx->finish_reason,sizeof(ctx->finish_reason),"%s",reason);}
    char delta[8192];
    if(json_string_after(json,"\"content\"",delta,sizeof(delta))&&delta[0]){
        append_stream_text(ctx,delta);ctx->chat->scroll_y=1000000;
        snprintf(status_text,sizeof(status_text),"正在生成 · %u 字节",(unsigned)strlen(ctx->assistant->text));
    }
    const char *image_part=strstr(json,"\"image_url\"");
    if(image_part){char image_url[1024];if(json_string_after(image_part,"\"url\"",image_url,sizeof(image_url))&&image_url[0]){append_stream_image(ctx,image_url);ctx->chat->scroll_y=1000000;}}
}

static int contains_ignore_case(const char *value,const char *needle) {
    if(!value||!needle||!*needle)return 0;
    size_t length=strlen(needle);
    for(const char *p=value;*p;p++){
        size_t i=0;
        while(i<length&&p[i]&&tolower((unsigned char)p[i])==tolower((unsigned char)needle[i]))i++;
        if(i==length)return 1;
    }
    return 0;
}

static int provider_is_minimax(void) {
    return contains_ignore_case(config.base_url,"minimax")||contains_ignore_case(config.model,"minimax");
}

static size_t stream_write(void *contents,size_t size,size_t count,void *user) {
    size_t incoming=size*count; StreamContext *ctx=user;
    if(incoming>=sizeof(ctx->pending))return 0;
    if(ctx->pending_length+incoming>=sizeof(ctx->pending))ctx->pending_length=0;
    memcpy(ctx->pending+ctx->pending_length,contents,incoming);ctx->pending_length+=incoming;ctx->pending[ctx->pending_length]=0;
    char *start=ctx->pending,*newline;
    while((newline=strchr(start,'\n'))){*newline=0;if(newline>start&&newline[-1]=='\r')newline[-1]=0;
        if(!strncmp(start,"data:",5)){ctx->saw_sse=1;process_stream_json(ctx,skip_space(start+5));}
        start=newline+1;
    }
    size_t remain=ctx->pending+ctx->pending_length-start;memmove(ctx->pending,start,remain);ctx->pending_length=remain;ctx->pending[remain]=0;return incoming;
}

static int fetch_models(void) {
    char url[640], error[256];
    char *response = calloc(1, HTTP_SIZE);
    if (!response) { snprintf(status_text,sizeof(status_text),"内存不足"); return 0; }
    endpoint_url(url, sizeof(url), "models");
    snprintf(status_text,sizeof(status_text),"正在获取模型……");
    if (!http_request(url, NULL, response, HTTP_SIZE, error, sizeof(error))) {
        snprintf(status_text,sizeof(status_text),"%s",error); free(response); return 0;
    }
    model_count=0; selected_model=0;
    const char *p=response;
    while(model_count<MAX_MODELS && (p=strstr(p,"\"id\""))) {
        if(json_string_after(p,"\"id\"",models[model_count],sizeof(models[0]))) model_count++;
        p += 4;
    }
    free(response);
    if(!model_count){snprintf(status_text,sizeof(status_text),"响应中没有可用模型");return 0;}
    for(int i=0;i<model_count;i++) if(!strcmp(models[i],config.model)) selected_model=i;
    snprintf(status_text,sizeof(status_text),"已加载 %d 个模型",model_count);
    current_view=VIEW_MODELS;
    return 1;
}

static CURLM *active_multi;
static CURL *active_easy;
static struct curl_slist *active_headers;
static char *active_body;
static StreamContext *active_stream;

static void cleanup_active_request(void) {
    if(active_multi&&active_easy)curl_multi_remove_handle(active_multi,active_easy);
    if(active_easy)curl_easy_cleanup(active_easy);
    if(active_multi)curl_multi_cleanup(active_multi);
    if(active_headers)curl_slist_free_all(active_headers);
    free(active_body);free(active_stream);
    active_easy=NULL;active_multi=NULL;active_headers=NULL;active_body=NULL;active_stream=NULL;is_generating=0;cancel_requested=0;
}

static void finish_active_request(CURLcode result,int stopped) {
    if(!active_stream){cleanup_active_request();return;}Message *assistant=active_stream->assistant;Chat *chat=active_stream->chat;long http=0;if(active_easy)curl_easy_getinfo(active_easy,CURLINFO_RESPONSE_CODE,&http);
    if(active_stream->pending_length){active_stream->pending[active_stream->pending_length]=0;const char *tail=skip_space(active_stream->pending);if(!strncmp(tail,"data:",5))tail=skip_space(tail+5);process_stream_json(active_stream,tail);}
    if(stopped)snprintf(status_text,sizeof(status_text),"已停止生成，已保留当前内容");
    else if(result!=CURLE_OK||http<200||http>=300)snprintf(status_text,sizeof(status_text),"请求失败：%s（HTTP %ld）",curl_easy_strerror(result),http);
    else if(active_stream->server_error[0])snprintf(status_text,sizeof(status_text),"服务端错误：%.190s",active_stream->server_error);
    else if(active_stream->overflow)snprintf(status_text,sizeof(status_text),"回复超过本地容量，已截断");
    else if(active_stream->saw_finish&&!strcmp(active_stream->finish_reason,"length"))snprintf(status_text,sizeof(status_text),"模型达到输出上限，回复未完成");
    else if(!active_stream->saw_done&&!active_stream->saw_finish)snprintf(status_text,sizeof(status_text),"连接已结束，但未收到完成标记");
    else if(!assistant->text[0])snprintf(status_text,sizeof(status_text),"服务端结束了请求，但没有返回正文");
    else snprintf(status_text,sizeof(status_text),"回复完成 · %u 字节",(unsigned)strlen(assistant->text));
    if(!assistant->text[0]&&chat->message_count>0)chat->message_count--;
    save_sessions();cleanup_active_request();
}

static void pump_network(void) {
    if(!is_generating||!active_multi)return;
    if(cancel_requested){finish_active_request(CURLE_ABORTED_BY_CALLBACK,1);return;}
    int running=0;CURLMcode mc=curl_multi_perform(active_multi,&running);
    if(mc!=CURLM_OK){finish_active_request(CURLE_RECV_ERROR,0);return;}
    int queued=0;CURLMsg *message;
    while((message=curl_multi_info_read(active_multi,&queued)))if(message->msg==CURLMSG_DONE){finish_active_request(message->data.result,0);return;}
}

static int send_chat(const char *prompt) {
    if(is_generating){snprintf(status_text,sizeof(status_text),"已有回复正在生成");return 0;}
    if (!config.base_url[0] || !config.model[0]) {
        snprintf(status_text,sizeof(status_text),"请先在设置中填写地址并选择模型");
        return 0;
    }
    if(chat_count<=0)add_chat();
    if(chat_count<=0||active_chat<0){snprintf(status_text,sizeof(status_text),"无法创建新会话");return 0;}
    Chat *chat=&chats[active_chat];
    if(chat->message_count+2>MAX_MESSAGES){snprintf(status_text,sizeof(status_text),"当前会话消息已满");return 0;}
    char *escaped=malloc(MESSAGE_SIZE*2),*body=calloc(1,HTTP_SIZE);if(!escaped||!body){free(escaped);free(body);snprintf(status_text,sizeof(status_text),"内存不足");return 0;}
    int responses_mode=config.web_search&&provider_is_minimax();
    const char *instruction="请直接、完整地回答用户问题，不要用以后可以补充之类的话提前结束。用户要求参数表或对照表时，必须在当前回复中给出完整 Markdown 表格。图片请使用 Markdown 图片语法并提供可公开访问的 HTTPS 地址。";
    size_t used;
    if(responses_mode){
        used=(size_t)snprintf(body,HTTP_SIZE,"{\"model\":\"%s\",\"stream\":true,\"max_output_tokens\":2048,\"instructions\":\"%s\",\"input\":[",config.model,instruction);
        for(int i=0;i<chat->message_count;i++){json_escape(chat->messages[i].text,escaped,MESSAGE_SIZE*2);used+=(size_t)snprintf(body+used,HTTP_SIZE-used,"%s{\"role\":\"%s\",\"content\":\"%s\"}",i?",":"",chat->messages[i].role,escaped);}
        json_escape(prompt,escaped,MESSAGE_SIZE*2);used+=(size_t)snprintf(body+used,HTTP_SIZE-used,"%s{\"role\":\"user\",\"content\":\"%s\"}],\"tools\":[{\"type\":\"web_search\"}]}",chat->message_count?",":"",escaped);
    }else{
        used=(size_t)snprintf(body,HTTP_SIZE,"{\"model\":\"%s\",\"stream\":true,\"max_tokens\":2048,\"messages\":[{\"role\":\"system\",\"content\":\"%s\"}",config.model,instruction);
        for(int i=0;i<chat->message_count;i++){json_escape(chat->messages[i].text,escaped,MESSAGE_SIZE*2);used+=(size_t)snprintf(body+used,HTTP_SIZE-used,",{\"role\":\"%s\",\"content\":\"%s\"}",chat->messages[i].role,escaped);}
        json_escape(prompt,escaped,MESSAGE_SIZE*2);used+=(size_t)snprintf(body+used,HTTP_SIZE-used,",{\"role\":\"user\",\"content\":\"%s\"}]%s}",escaped,config.web_search?",\"enable_search\":true":"");
    }
    free(escaped);
    if(used>=HTTP_SIZE){free(body);snprintf(status_text,sizeof(status_text),"请求内容过长");return 0;}
    Message *user=&chat->messages[chat->message_count++]; strcpy(user->role,"user"); snprintf(user->text,sizeof(user->text),"%s",prompt);
    Message *assistant=&chat->messages[chat->message_count++]; strcpy(assistant->role,"assistant");assistant->text[0]=0;
    if(chat->message_count==2) snprintf(chat->title,sizeof(chat->title),"%.42s",prompt);
    active_stream=calloc(1,sizeof(*active_stream));active_easy=curl_easy_init();active_multi=curl_multi_init();if(!active_stream||!active_easy||!active_multi){free(body);chat->message_count-=2;cleanup_active_request();snprintf(status_text,sizeof(status_text),"无法初始化异步网络请求");return 0;}active_body=body;active_stream->assistant=assistant;active_stream->chat=chat;active_stream->responses_mode=responses_mode;char url[640];endpoint_url(url,sizeof(url),responses_mode?"responses":"chat/completions");active_headers=curl_slist_append(active_headers,"Accept: text/event-stream");active_headers=curl_slist_append(active_headers,"Content-Type: application/json");char auth[640];if(config.api_key[0]){snprintf(auth,sizeof(auth),"Authorization: Bearer %s",config.api_key);active_headers=curl_slist_append(active_headers,auth);}curl_easy_setopt(active_easy,CURLOPT_URL,url);curl_easy_setopt(active_easy,CURLOPT_HTTPHEADER,active_headers);curl_easy_setopt(active_easy,CURLOPT_POSTFIELDS,active_body);curl_easy_setopt(active_easy,CURLOPT_WRITEFUNCTION,stream_write);curl_easy_setopt(active_easy,CURLOPT_WRITEDATA,active_stream);curl_easy_setopt(active_easy,CURLOPT_BUFFERSIZE,1024L);curl_easy_setopt(active_easy,CURLOPT_TCP_NODELAY,1L);curl_easy_setopt(active_easy,CURLOPT_CONNECTTIMEOUT,15L);curl_easy_setopt(active_easy,CURLOPT_TIMEOUT,120L);curl_easy_setopt(active_easy,CURLOPT_USERAGENT,CLIENT_USER_AGENT);if(curl_multi_add_handle(active_multi,active_easy)!=CURLM_OK){chat->message_count-=2;cleanup_active_request();snprintf(status_text,sizeof(status_text),"无法加入异步网络队列");return 0;}chat->scroll_y=1000000;cancel_requested=0;is_generating=1;snprintf(status_text,sizeof(status_text),config.web_search?"正在联网搜索并生成……":"正在连接 %s……",config.model);return 1;
}

static void clean_markdown_inline(const char *input, char *output, size_t size) {
    size_t used=0;
    for(size_t i=0; input[i] && used+1<size; i++) {
        if(input[i]=='\\'&&input[i+1]&&strchr("*`_[]()#!>-",input[i+1])){output[used++]=input[++i];continue;}
        if(input[i]=='*' && input[i+1]=='*'){i++;continue;}
        if(input[i]=='_' && input[i+1]=='_'){i++;continue;}
        if(input[i]=='*'||input[i]=='_'||input[i]=='`')continue;
        if(input[i]=='['){const char *close=strstr(input+i,"](");if(close){const char *end=strchr(close+2,')');if(end){for(const char *p=input+i+1;p<close&&used+1<size;p++)output[used++]=*p;i=(size_t)(end-input);continue;}}}
        output[used++]=input[i];
    }
    output[used]=0;
}

static int draw_inline_markdown(SDL_Renderer *r,TTF_Font *font,const char *value,int x,int y,int width,SDL_Color color,int force_bold) {
    int bold=force_bold||strstr(value,"**")||strstr(value,"__");char plain[MESSAGE_SIZE];clean_markdown_inline(value,plain,sizeof(plain));int old=TTF_GetFontStyle(font);if(bold)TTF_SetFontStyle(font,old|TTF_STYLE_BOLD);int h=draw_wrapped(r,font,plain,x,y,width,color);if(bold)TTF_SetFontStyle(font,old);return h;
}

static void strip_think_blocks(const char *input,char *output,size_t size) {
    size_t used=0;int hidden=0;
    while(*input&&used+1<size){
        if(!strncmp(input,"<think>",7)){hidden=1;input+=7;continue;}
        if(!strncmp(input,"</think>",8)){hidden=0;input+=8;continue;}
        if(!hidden)output[used++]=*input;
        input++;
    }
    output[used]=0;
}

static char *trim_cell(char *text) {
    while(*text&&isspace((unsigned char)*text))text++;
    char *end=text+strlen(text);while(end>text&&isspace((unsigned char)end[-1]))*--end=0;return text;
}

static int table_separator(const char *line) {
    int dashes=0;
    for(const char *p=line;*p;p++){if(*p=='-')dashes++;else if(*p!='|'&&*p!=':'&&!isspace((unsigned char)*p))return 0;}
    return dashes>=3;
}

static int split_cells(char *line,char **cells,int max_cells) {
    char *value=trim_cell(line);if(*value=='|')value++;size_t n=strlen(value);while(n&&isspace((unsigned char)value[n-1]))value[--n]=0;if(n&&value[n-1]=='|')value[--n]=0;
    int count=0;char *save=NULL,*cell=strtok_r(value,"|",&save);while(cell&&count<max_cells){cells[count++]=trim_cell(cell);cell=strtok_r(NULL,"|",&save);}return count;
}

static int draw_table_row(SDL_Renderer *r,TTF_Font *font,char **cells,int count,int x,int y,int width,Theme t,int header) {
    if(count<1)return 0;
    int cell_w=width/count,row_h=38;
    for(int i=0;i<count;i++){int h=draw_inline_markdown(NULL,font,cells[i],0,0,cell_w-16,t.text,header)+14;if(h>row_h)row_h=h;}
    if(r){Uint8 header_bg[3]={t.selected[0],t.selected[1],t.selected[2]};Uint8 row_bg[3]={t.panel[0],t.panel[1],t.panel[2]};fill(r,x,y,width,row_h,header?header_bg:row_bg);SDL_SetRenderDrawColor(r,t.muted.r,t.muted.g,t.muted.b,150);SDL_RenderDrawLine(r,x,y,x+width,y);SDL_RenderDrawLine(r,x,y+row_h-1,x+width,y+row_h-1);for(int i=0;i<=count;i++)SDL_RenderDrawLine(r,x+i*cell_w,y,x+i*cell_w,y+row_h);for(int i=0;i<count;i++)draw_inline_markdown(r,font,cells[i],x+i*cell_w+8,y+7,cell_w-16,header?t.accent:t.text,header);}
    return row_h;
}

static int draw_mermaid(SDL_Renderer *r,TTF_Font *font,char **lines,int start,int end,int x,int y,int width,Theme t) {
    char labels[12][96];int count=0;
    for(int row=start;row<end&&count<12;row++){
        const char *p=lines[row];
        while(*p&&count<12){char open=0,close=0;if(*p=='['){open='[';close=']';}else if(*p=='{'){open='{';close='}';}else if(*p=='('){open='(';close=')';}if(!open){p++;continue;}const char *finish=strchr(p+1,close);if(!finish){p++;continue;}size_t len=(size_t)(finish-p-1);if(len>0&&len<sizeof(labels[0])){char candidate[96];memcpy(candidate,p+1,len);candidate[len]=0;int duplicate=0;for(int i=0;i<count;i++)if(!strcmp(labels[i],candidate))duplicate=1;if(!duplicate)snprintf(labels[count++],sizeof(labels[0]),"%s",candidate);}p=finish+1;}
    }
    if(!count)return 0;
    int diagram_h=48+count*68;
    if(r){Uint8 diagram_bg[3]={t.panel[0],t.panel[1],t.panel[2]};fill_rounded(r,x,y,width,diagram_h,12,diagram_bg);draw_text(r,font,"流程图",x+16,y+9,t.accent);int center=x+width/2;for(int i=0;i<count;i++){int node_y=y+42+i*68,node_w=width-80,node_x=x+40;if(i>0){SDL_SetRenderDrawColor(r,t.accent.r,t.accent.g,t.accent.b,255);SDL_RenderDrawLine(r,center,node_y-24,center,node_y-5);SDL_RenderDrawLine(r,center,node_y-5,center-5,node_y-12);SDL_RenderDrawLine(r,center,node_y-5,center+5,node_y-12);}fill_rounded(r,node_x,node_y,node_w,44,11,i%2?t.card:t.selected);draw_inline_markdown(r,font,labels[i],node_x+12,node_y+8,node_w-24,t.text,0);}}
    return diagram_h;
}

static int parse_markdown_image_line(const char *line,char *alt,size_t alt_size,char *url,size_t url_size) {
    while(*line&&isspace((unsigned char)*line))line++;
    if(strncmp(line,"![",2))return 0;
    const char *label_end=strstr(line+2,"](");if(!label_end)return 0;
    const char *url_start=label_end+2,*url_end=strrchr(url_start,')');if(!url_end)return 0;
    const char *tail=url_end+1;while(*tail&&isspace((unsigned char)*tail))tail++;if(*tail)return 0;
    while(url_start<url_end&&isspace((unsigned char)*url_start))url_start++;
    while(url_end>url_start&&isspace((unsigned char)url_end[-1]))url_end--;
    if(url_end>url_start+1&&url_start[0]=='<'&&url_end[-1]=='>'){url_start++;url_end--;}
    size_t label_len=(size_t)(label_end-(line+2));if(label_len>=alt_size)label_len=alt_size-1;memcpy(alt,line+2,label_len);alt[label_len]=0;
    size_t url_len=(size_t)(url_end-url_start);if(!url_len||url_len>=url_size)return 0;memcpy(url,url_start,url_len);url[url_len]=0;
    return 1;
}

static int markdown_contains_image(const char *text) {
    const char *line=text;
    while(line&&*line){while(*line==' '||*line=='\t')line++;if(!strncmp(line,"![",2))return 1;line=strchr(line,'\n');if(line)line++;}
    return 0;
}

static int draw_markdown_image(SDL_Renderer *r,TTF_Font *font,const char *alt,const char *url,int x,int y,int width,Theme t) {
    ImageEntry *entry=start_image_download(url);int image_h=IMAGE_PLACEHOLDER_H,display_w=width,display_h=IMAGE_PLACEHOLDER_H;
    if(entry&&entry->state==IMAGE_READY&&entry->width>0&&entry->height>0){display_w=entry->width<width?entry->width:width;display_h=(int)((long long)entry->height*display_w/entry->width);if(display_h>330){display_w=(int)((long long)display_w*330/display_h);display_h=330;}if(display_h<1)display_h=1;image_h=display_h;}
    int caption_h=alt[0]?draw_wrapped(NULL,font,alt,0,0,width-24,t.muted)+8:0;int total=image_h+caption_h+20;
    if(!r)return total;
    Uint8 image_bg[3]={t.panel[0],t.panel[1],t.panel[2]};fill_rounded(r,x,y,width,total,12,image_bg);
    if(entry&&entry->state==IMAGE_READY&&entry->texture){SDL_Rect target={x+(width-display_w)/2,y+10,display_w,display_h};SDL_RenderCopy(r,entry->texture,NULL,&target);}
    else {const char *label="正在加载图片…";SDL_Color color=t.accent;if(!entry){label="图片缓存暂时不可用";color=t.danger;}else if(entry->state==IMAGE_FAILED){label=entry->error[0]?entry->error:"图片加载失败";color=t.danger;}draw_text(r,font,label,x+18,y+18,color);draw_wrapped(r,font,url,x+18,y+58,width-36,t.muted);}
    if(alt[0])draw_wrapped(r,font,alt,x+12,y+10+image_h,width-24,t.muted);
    return total;
}

static int draw_markdown(SDL_Renderer *r, TTF_Font *font, TTF_Font *small,
                         const char *source, int x, int y, int width, Theme t) {
    char buffer[MESSAGE_SIZE];strip_think_blocks(source,buffer,sizeof(buffer));
    char *lines[256];int line_count=0;char *save=NULL,*part=strtok_r(buffer,"\n",&save);while(part&&line_count<256){lines[line_count++]=part;part=strtok_r(NULL,"\n",&save);}
    int cursor=y;
    for(int index=0;index<line_count;index++){
        char *line=lines[index];
        char image_alt[160],image_url[1024];
        if(parse_markdown_image_line(line,image_alt,sizeof(image_alt),image_url,sizeof(image_url))){cursor+=draw_markdown_image(r,small,image_alt,image_url,x,cursor,width,t)+8;continue;}
        if(strchr(line,'|')&&index+1<line_count&&table_separator(lines[index+1])){
            char header_copy[MESSAGE_SIZE];snprintf(header_copy,sizeof(header_copy),"%s",line);char *cells[8];int columns=split_cells(header_copy,cells,8);cursor+=draw_table_row(r,small,cells,columns,x,cursor,width,t,1);index+=2;
            while(index<line_count&&strchr(lines[index],'|')){char row_copy[MESSAGE_SIZE];snprintf(row_copy,sizeof(row_copy),"%s",lines[index]);char *row_cells[8];int row_count=split_cells(row_copy,row_cells,8);while(row_count<columns)row_cells[row_count++]="";cursor+=draw_table_row(r,small,row_cells,columns,x,cursor,width,t,0);index++;}index--;cursor+=8;continue;
        }
        if(!strncmp(line,"```",3)){
            int end=index+1;while(end<line_count&&strncmp(lines[end],"```",3))end++;
            const char *language=trim_cell(line+3);
            if(!strcmp(language,"mermaid")){int diagram_h=draw_mermaid(r,small,lines,index+1,end,x,cursor,width,t);if(diagram_h){cursor+=diagram_h+8;index=end;continue;}}
            int block_h=38;for(int row=index+1;row<end;row++)block_h+=draw_wrapped(NULL,small,lines[row],0,0,width-28,t.text)+3;
            if(r){fill_rounded(r,x,cursor,width,block_h,10,t.panel);draw_text(r,small,language[0]?language:"代码",x+14,cursor+7,t.muted);}
            int code_y=cursor+34;for(int row=index+1;row<end;row++){int h=draw_wrapped(r,small,lines[row],x+14,code_y,width-28,t.accent);code_y+=h+3;}
            cursor+=block_h+8;index=end;continue;
        }
        if((!strcmp(line,"---")||!strcmp(line,"***"))){if(r){SDL_SetRenderDrawColor(r,t.muted.r,t.muted.g,t.muted.b,150);SDL_RenderDrawLine(r,x,cursor+8,x+width,cursor+8);}cursor+=20;continue;}
        char clean[MESSAGE_SIZE]; const char *value=line; TTF_Font *use=small; SDL_Color color=t.text;
        if(!strncmp(value,"### ",4)){value+=4;use=font;}
        else if(!strncmp(value,"## ",3)){value+=3;use=font;}
        else if(!strncmp(value,"# ",2)){value+=2;use=font;color=t.accent;}
        else if(!strncmp(value,"- ",2)){snprintf(clean,sizeof(clean),"• %s",value+2);value=clean;}
        else if(!strncmp(value,"> ",2)){snprintf(clean,sizeof(clean),"❯ %s",value+2);value=clean;color=t.accent;}
        int h=draw_inline_markdown(r,use,value,x,cursor,width,color,0);cursor+=h?h+5:26;
    }
    return cursor-y;
}

static int message_height(TTF_Font *small,const char *text,int width) {
    if(!text||!text[0])return 64;
    int h=draw_markdown(NULL,live_font,small,text,0,0,width-40,get_theme())+46;if(h<72)h=72;return h;
}

static unsigned text_hash(const char *text) {
    unsigned hash=2166136261u;for(const unsigned char *p=(const unsigned char*)text;*p;p++){hash^=*p;hash*=16777619u;}return hash;
}

static int cached_message_height(int chat_index,int message_index,TTF_Font *small,int width) {
    unsigned hash=text_hash(chats[chat_index].messages[message_index].text);
    if(markdown_contains_image(chats[chat_index].messages[message_index].text))hash^=image_layout_revision*16777619u;
    if(height_cache_hash[chat_index][message_index]!=hash||height_cache_width[chat_index][message_index]!=width){height_cache_hash[chat_index][message_index]=hash;height_cache_width[chat_index][message_index]=width;height_cache_value[chat_index][message_index]=message_height(small,chats[chat_index].messages[message_index].text,width);}
    return height_cache_value[chat_index][message_index];
}

static SDL_Texture *cached_bubble(SDL_Renderer *r,int chat_index,int message_index,TTF_Font *font,TTF_Font *small,int width,int height,Theme t,int user) {
    BubbleCache *cache=&bubble_cache[chat_index][message_index];unsigned hash=text_hash(chats[chat_index].messages[message_index].text);
    if(cache->texture&&(cache->hash!=hash||cache->width!=width||cache->height!=height||cache->theme_id!=config.theme_id||cache->user!=user)){SDL_DestroyTexture(cache->texture);memset(cache,0,sizeof(*cache));}
    if(!cache->texture){SDL_Surface *canvas=SDL_CreateRGBSurfaceWithFormat(0,width,height,32,SDL_PIXELFORMAT_RGBA32);if(!canvas)return NULL;SDL_Renderer *software=SDL_CreateSoftwareRenderer(canvas);if(!software){SDL_FreeSurface(canvas);return NULL;}SDL_SetRenderDrawColor(software,0,0,0,0);SDL_RenderClear(software);Uint8 user_color[3]={t.selected[0],t.selected[1],t.selected[2]};fill_rounded(software,0,0,width,height,16,user?user_color:t.card);draw_text(software,small,user?"你":"AI",18,9,user?t.text:t.accent);draw_markdown(software,font,small,chats[chat_index].messages[message_index].text,18,36,width-36,t);SDL_RenderPresent(software);SDL_Texture *texture=SDL_CreateTextureFromSurface(r,canvas);SDL_DestroyRenderer(software);SDL_FreeSurface(canvas);if(!texture)return NULL;SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);cache->texture=texture;cache->hash=hash;cache->width=width;cache->height=height;cache->theme_id=config.theme_id;cache->user=user;}
    return cache->texture;
}

static void destroy_bubble_cache(void) {
    for(int i=0;i<MAX_CHATS;i++)for(int j=0;j<MAX_MESSAGES;j++)if(bubble_cache[i][j].texture){SDL_DestroyTexture(bubble_cache[i][j].texture);bubble_cache[i][j].texture=NULL;}
}

static void draw_sidebar_chevron(SDL_Renderer *r,int center_x,int center_y,int points_right,SDL_Color color) {
    SDL_SetRenderDrawColor(r,color.r,color.g,color.b,color.a);
    for(int offset=-1;offset<=1;offset++) {
        int outer_x=center_x+(points_right?-5:5)+offset;
        int tip_x=center_x+(points_right?5:-5)+offset;
        SDL_RenderDrawLine(r,outer_x,center_y-9,tip_x,center_y);
        SDL_RenderDrawLine(r,tip_x,center_y,outer_x,center_y+9);
    }
}

static void draw_sidebar_actions(SDL_Renderer *r,TTF_Font *small,Theme t,int side) {
    if(sidebar_collapsed){
        fill_rounded(r,10,606,side-20,46,12,t.selected);
        fill_rounded(r,10,660,side-20,46,12,t.card);
        draw_text(r,small,"+",27,615,t.text);
        draw_text(r,small,"设",25,669,t.text);
    }else{
        int button_w=(side-30)/2;
        fill_rounded(r,10,660,button_w,46,12,t.selected);
        fill_rounded(r,20+button_w,660,button_w,46,12,t.card);
        draw_text(r,small,"+ 新建",35,669,t.text);
        draw_text(r,small,"设置",20+button_w+48,669,t.text);
    }
}

static void draw_composer(SDL_Renderer *r,TTF_Font *small,Theme t,int content_x,int content_w) {
    int composer_y=550,button_w=126,input_w=content_w-button_w-14;
    fill_rounded(r,content_x,composer_y,input_w,72,16,t.panel);
    SDL_Rect input_clip={content_x+16,composer_y+8,input_w-32,55};SDL_RenderSetClipRect(r,&input_clip);draw_wrapped(r,small,draft_text[0]?draft_text:"轻触这里输入消息…",content_x+18,composer_y+17,input_w-36,draft_text[0]?t.text:t.muted);SDL_RenderSetClipRect(r,NULL);
    Uint8 action_color[3]={is_generating?t.danger.r:t.accent.r,is_generating?t.danger.g:t.accent.g,is_generating?t.danger.b:t.accent.b};fill_rounded(r,content_x+input_w+14,composer_y,button_w,72,16,action_color);draw_text(r,small,is_generating?"停止":"发送",content_x+input_w+(is_generating?51:53),composer_y+21,(SDL_Color){255,255,255,255});
    draw_text(r,small,is_generating?"B 停止生成":"A编辑  B发送  L/R会话  ZL改名  X新建  Y删除  左键收起  右键展开  ZR设置",content_x+4,636,t.muted);
    draw_wrapped(r,small,status_text,content_x+4,680,content_w-8,t.accent);
}

static void draw_chat(SDL_Renderer *r, TTF_Font *font, TTF_Font *small, Theme t) {
    if(bubble_cache_chat!=active_chat){destroy_bubble_cache();bubble_cache_chat=active_chat;}
    int side=sidebar_collapsed?72:330,content_x=side+40,content_w=SCREEN_W-content_x-45;
    fill(r,0,0,SCREEN_W,SCREEN_H,t.bg);fill(r,0,0,side,SCREEN_H,t.panel);
    Uint8 accent_fill[3]={t.accent.r,t.accent.g,t.accent.b};
    fill(r,0,0,4,SCREEN_H,accent_fill);draw_text(r,font,sidebar_collapsed?"AI":"Switch AI Chat",sidebar_collapsed?18:26,22,t.text);
    fill_rounded(r,side-15,326,30,64,15,t.selected);
    draw_sidebar_chevron(r,side,358,sidebar_collapsed,t.text);
    for(int i=0;i<chat_count;i++){int row_y=88+i*68;if(i==active_chat){fill_rounded(r,10,row_y-7,side-20,60,12,t.selected);fill(r,10,row_y-7,4,60,accent_fill);}if(!sidebar_collapsed){draw_text(r,small,chats[i].title,26,row_y,t.text);draw_text(r,small,chats[i].created_at,26,row_y+28,t.muted);}else draw_text(r,small,i==active_chat?"●":"○",25,row_y+9,i==active_chat?t.accent:t.muted);}
    draw_sidebar_actions(r,small,t,side);
    int search_x=content_x+content_w-172;
    Uint8 search_color[3]={config.web_search?t.accent.r:t.card[0],config.web_search?t.accent.g:t.card[1],config.web_search?t.accent.b:t.card[2]};
    fill_rounded(r,search_x,46,172,34,12,search_color);
    draw_text(r,small,config.web_search?"联网搜索：开":"联网搜索：关",search_x+18,51,config.web_search?(SDL_Color){255,255,255,255}:t.muted);
    if(chat_count<=0||active_chat<0){
        draw_text(r,font,"开始新对话",content_x,24,t.text);
        fill_rounded(r,content_x+10,112,content_w-20,170,20,t.card);
        draw_text(r,font,"目前还没有会话",content_x+48,145,t.text);
        draw_wrapped(r,small,"点击侧栏底部的“新建”按钮，或直接输入消息并发送。",content_x+48,202,content_w-96,t.muted);
        draw_composer(r,small,t,content_x,content_w);
        return;
    }
    const char *rename_hint="点按标题 / ZL 重命名";int hint_w=0,hint_h=0;TTF_SizeUTF8(small,rename_hint,&hint_w,&hint_h);
    int title_w=content_w-hint_w-32;if(title_w<180)title_w=180;
    SDL_Rect title_clip={content_x,10,title_w,40};SDL_RenderSetClipRect(r,&title_clip);
    draw_text(r,font,chats[active_chat].title,content_x,18,t.text);SDL_RenderSetClipRect(r,NULL);
    draw_text(r,small,rename_hint,content_x+content_w-hint_w,24,t.muted);
    draw_text(r,small,chats[active_chat].created_at,content_x,51,t.muted);
    Chat *chat=&chats[active_chat];int total=0;
    for(int i=0;i<chat->message_count;i++){int bubble_w=!strcmp(chat->messages[i].role,"user")?content_w*3/4:content_w*17/20;total+=cached_message_height(active_chat,i,small,bubble_w)+22;}
    int viewport_top=84,viewport_bottom=535,max_scroll=total-(viewport_bottom-viewport_top);if(max_scroll<0)max_scroll=0;if(chat->scroll_y<0){chat->scroll_y=0;if(scroll_velocity<0)scroll_velocity=0;}if(chat->scroll_y>max_scroll){chat->scroll_y=max_scroll;if(scroll_velocity>0)scroll_velocity=0;}
    if(!chat->message_count)draw_text(r,small,"按 A 输入消息；上下键或触摸拖动滚动",content_x,115,t.muted);
    SDL_Rect clip={content_x,viewport_top,content_w,viewport_bottom-viewport_top};SDL_RenderSetClipRect(r,&clip);
    int y=viewport_top-chat->scroll_y;
    for(int i=0;i<chat->message_count;i++){int user=!strcmp(chat->messages[i].role,"user");int bubble_w=user?content_w*3/4:content_w*17/20;int bx=user?content_x+content_w-bubble_w-10:content_x+10;int card_h=cached_message_height(active_chat,i,small,bubble_w);if(y+card_h>=viewport_top&&y<=viewport_bottom){int has_image=markdown_contains_image(chat->messages[i].text);SDL_Texture *bubble=has_image?NULL:cached_bubble(r,active_chat,i,font,small,bubble_w,card_h,t,user);if(bubble){SDL_Rect target={bx,y,bubble_w,card_h};SDL_RenderCopy(r,bubble,NULL,&target);}else{Uint8 user_color[3]={t.selected[0],t.selected[1],t.selected[2]};fill_rounded(r,bx,y,bubble_w,card_h,16,user?user_color:t.card);draw_text(r,small,user?"你":"AI",bx+18,y+9,user?t.text:t.accent);draw_markdown(r,font,small,chat->messages[i].text,bx+18,y+36,bubble_w-36,t);}}y+=card_h+22;}
    SDL_RenderSetClipRect(r,NULL);
    if(max_scroll>0){int track_h=viewport_bottom-viewport_top;int thumb_h=track_h*track_h/(total?total:1);if(thumb_h<42)thumb_h=42;int thumb_y=viewport_top+(track_h-thumb_h)*chat->scroll_y/max_scroll;fill_rounded(r,SCREEN_W-18,viewport_top,5,track_h,2,t.card);fill_rounded(r,SCREEN_W-19,thumb_y,7,thumb_h,3,accent_fill);}
    draw_composer(r,small,t,content_x,content_w);
    if(confirm_delete){fill_rounded(r,355,245,570,190,22,t.card);draw_text(r,font,"确认删除当前会话？",410,285,t.text);draw_text(r,small,"A 确认    B 取消",430,350,t.muted);}
}

static void masked_key(char *output,size_t size){size_t n=strlen(config.api_key);if(!n){snprintf(output,size,"未填写");return;}snprintf(output,size,"********%s",n>4?config.api_key+n-4:"");}

static void draw_settings(SDL_Renderer *r, TTF_Font *font, TTF_Font *small, Theme t) {
    fill(r,0,0,SCREEN_W,SCREEN_H,t.bg); draw_text(r,font,"设置",54,35,t.text);
    const char *labels[]={"Base URL","API Key","获取可用模型","外观主题","联网搜索","返回聊天"};
    char values[6][540]; char masked[64]; masked_key(masked,sizeof(masked));
    snprintf(values[0],sizeof(values[0]),"%s",config.base_url[0]?config.base_url:"未填写");
    snprintf(values[1],sizeof(values[1]),"%s",masked); snprintf(values[2],sizeof(values[2]),"当前模型：%s",config.model[0]?config.model:"未选择");
    snprintf(values[3],sizeof(values[3]),"%s（A 切换）",theme_name());
    snprintf(values[4],sizeof(values[4]),"%s（MiniMax / 千问自动适配）",config.web_search?"已开启":"已关闭");
    snprintf(values[5],sizeof(values[5]),"保存并返回");
    for(int i=0;i<6;i++){int y=104+i*82;if(i==setting_row)fill(r,42,y-10,1190,68,t.selected);draw_text(r,small,labels[i],70,y,t.text);draw_wrapped(r,small,values[i],330,y,860,i==setting_row?t.text:t.muted);}
    draw_text(r,small,"方向键选择   A 编辑/确认   B 返回",55,640,t.muted); draw_wrapped(r,small,status_text,55,680,1160,t.accent);
}

static void draw_models(SDL_Renderer *r, TTF_Font *font, TTF_Font *small, Theme t) {
    fill(r,0,0,SCREEN_W,SCREEN_H,t.bg); draw_text(r,font,"选择模型",48,30,t.text);
    int first=selected_model-6;if(first<0)first=0;if(first>model_count-11)first=model_count-11;if(first<0)first=0;
    for(int row=0;row<11 && first+row<model_count;row++){int index=first+row,y=92+row*48;if(index==selected_model)fill(r,34,y-7,1210,43,t.selected);draw_text(r,small,models[index],55,y,t.text);}
    draw_text(r,small,"↑/↓ 选择   A 使用此模型   B 返回设置",48,652,t.muted);
}

static void handle_settings(u64 down) {
    if(down&HidNpadButton_Up && setting_row>0)setting_row--;
    if(down&HidNpadButton_Down && setting_row<5)setting_row++;
    if(down&HidNpadButton_B){save_config();current_view=VIEW_CHAT;return;}
    if(!(down&HidNpadButton_A))return;
    if(setting_row==0){if(keyboard_input(config.base_url,sizeof(config.base_url),"输入 Base URL",0)){save_config();snprintf(status_text,sizeof(status_text),"Base URL 已保存");}}
    else if(setting_row==1){if(keyboard_input(config.api_key,sizeof(config.api_key),"输入 API Key",1)){save_config();snprintf(status_text,sizeof(status_text),"API Key 已保存");}}
    else if(setting_row==2)fetch_models();
    else if(setting_row==3){if(config.theme_id<0||config.theme_id>=THEME_COUNT)config.theme_id=0;else config.theme_id=(config.theme_id+1)%THEME_COUNT;destroy_bubble_cache();save_config();snprintf(status_text,sizeof(status_text),"主题已切换为：%s",theme_name());}
    else if(setting_row==4){config.web_search=!config.web_search;save_config();snprintf(status_text,sizeof(status_text),"联网搜索已%s",config.web_search?"开启":"关闭");}
    else {save_config();current_view=VIEW_CHAT;}
}

static u64 read_touch_action(int *touch_was_down) {
    HidTouchScreenState state;
    size_t states=hidGetTouchScreenStates(&state,1);
    if(!states||state.count<=0){*touch_was_down=0;return 0;}
    int x=(int)state.touches[0].x,y=(int)state.touches[0].y;
    if(*touch_was_down){
        if(current_view==VIEW_CHAT&&chat_count>0&&active_chat>=0&&x>(sidebar_collapsed?72:330)&&y>=84&&y<=535){int delta=touch_last_y-y;chats[active_chat].scroll_y+=delta;scroll_velocity=(float)delta;touch_last_y=y;}
        else touch_last_y=y;
        return 0;
    }
    *touch_was_down=1;touch_last_y=y;
    if(current_view==VIEW_CHAT){
        int side=sidebar_collapsed?72:330;
        int content_x=side+40,content_w=SCREEN_W-content_x-45,search_x=content_x+content_w-172;
        if(x>=side-24&&x<=side+24&&y>=315&&y<=402){sidebar_collapsed=!sidebar_collapsed;return 0;}
        if(!sidebar_collapsed&&x<side&&y>=650&&y<=716)return x<side/2?HidNpadButton_X:HidNpadButton_ZR;
        if(sidebar_collapsed&&x<side&&y>=598&&y<657)return HidNpadButton_X;
        if(sidebar_collapsed&&x<side&&y>=657&&y<=716)return HidNpadButton_ZR;
        if(x>=search_x&&x<=search_x+172&&y>=42&&y<=84){config.web_search=!config.web_search;save_config();snprintf(status_text,sizeof(status_text),"联网搜索已%s",config.web_search?"开启":"关闭");return 0;}
        if(chat_count>0&&x>side&&y<78)return HidNpadButton_ZL;
        if(x<side&&y>=83&&y<83+chat_count*68){int index=(y-83)/68;if(index>=0&&index<chat_count)active_chat=index;return 0;}
        if(x>side&&y>=548&&y<=630){if(x>=SCREEN_W-180)return HidNpadButton_B;return HidNpadButton_A;}
    } else if(current_view==VIEW_SETTINGS){
        for(int i=0;i<6;i++){int top=94+i*82;if(y>=top&&y<top+70){setting_row=i;return HidNpadButton_A;}}
    } else {
        int first=selected_model-6;if(first<0)first=0;if(first>model_count-11)first=model_count-11;if(first<0)first=0;
        if(y>=85&&y<620){int row=(y-85)/48,index=first+row;if(index>=0&&index<model_count){selected_model=index;return HidNpadButton_A;}}
    }
    return 0;
}

int main(void) {
    fsdevMountSdmc();romfsInit();load_config();
    if(R_FAILED(socketInitializeDefault())) return 1;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    PlFontData font_data,standard_data;
    if(R_FAILED(plGetSharedFontByType(&font_data,PlSharedFontType_ChineseSimplified))) return 1;
    int has_standard=R_SUCCEEDED(plGetSharedFontByType(&standard_data,PlSharedFontType_Standard));
    if(SDL_Init(SDL_INIT_VIDEO)!=0 || TTF_Init()!=0) return 1;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"1");
    SDL_Window *window=SDL_CreateWindow("Switch AI Chat",0,0,SCREEN_W,SCREEN_H,0);
    SDL_Renderer *renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
    TTF_Font *font=TTF_OpenFontRW(SDL_RWFromConstMem(font_data.address,(int)font_data.size),0,28);
    TTF_Font *small=TTF_OpenFontRW(SDL_RWFromConstMem(font_data.address,(int)font_data.size),0,21);
    TTF_Font *symbol=has_standard?TTF_OpenFontRW(SDL_RWFromConstMem(standard_data.address,(int)standard_data.size),0,21):NULL;
    TTF_Font *emoji=TTF_OpenFont("romfs:/NotoEmoji.ttf",21);
    if(!window||!renderer||!font||!small)return 1;
    if(!load_sessions()){chat_count=0;active_chat=-1;}
    if(chat_count==0)snprintf(status_text,sizeof(status_text),"点击新建会话，或直接输入消息开始");
    padConfigureInput(1,HidNpadStyleSet_NpadStandard); PadState pad; padInitializeDefault(&pad);hidInitializeTouchScreen();int touch_was_down=0;
    live_font=font;live_symbol=symbol;live_emoji=emoji;
    while(appletMainLoop()){
        pump_network();pump_image_downloads(renderer);padUpdate(&pad);u64 down=padGetButtonsDown(&pad)|read_touch_action(&touch_was_down);u64 held=padGetButtons(&pad);if(is_generating&&(down&HidNpadButton_B))cancel_requested=1;if(down&HidNpadButton_Plus){if(is_generating)finish_active_request(CURLE_ABORTED_BY_CALLBACK,1);break;}
        if(current_view==VIEW_CHAT){
            if(confirm_delete){if(down&HidNpadButton_A){delete_chat();confirm_delete=0;}if(down&HidNpadButton_B)confirm_delete=0;}
            else {if((down&HidNpadButton_L)&&chat_count>1)active_chat=(active_chat+chat_count-1)%chat_count;if((down&HidNpadButton_R)&&chat_count>1)active_chat=(active_chat+1)%chat_count;if(down&HidNpadButton_Left)sidebar_collapsed=1;if(down&HidNpadButton_Right)sidebar_collapsed=0;if(chat_count>0&&active_chat>=0){if(held&HidNpadButton_Up)scroll_velocity=-7.0f;if(held&HidNpadButton_Down)scroll_velocity=7.0f;HidAnalogStickState stick=padGetStickPos(&pad,0);if(stick.y>5000||stick.y<-5000)scroll_velocity=-(float)stick.y/2400.0f;if(!touch_was_down&&scroll_velocity!=0.0f){chats[active_chat].scroll_y+=(int)scroll_velocity;scroll_velocity*=0.88f;if(scroll_velocity>-0.35f&&scroll_velocity<0.35f)scroll_velocity=0.0f;}}else scroll_velocity=0.0f;if(down&HidNpadButton_ZL)rename_chat();if(down&HidNpadButton_X)add_chat();if((down&HidNpadButton_Y)&&chat_count>0&&!is_generating)confirm_delete=1;if(down&HidNpadButton_ZR){current_view=VIEW_SETTINGS;setting_row=0;}if(down&HidNpadButton_A){if(keyboard_input(draft_text,sizeof(draft_text),"输入消息",0))snprintf(status_text,sizeof(status_text),"内容已就绪，按 B 或轻触发送");}if((down&HidNpadButton_B)&&draft_text[0]&&!is_generating){char prompt[MESSAGE_SIZE];snprintf(prompt,sizeof(prompt),"%s",draft_text);draft_text[0]=0;send_chat(prompt);}}
        } else if(current_view==VIEW_SETTINGS) handle_settings(down);
        else {if((down&HidNpadButton_Up)&&selected_model>0)selected_model--;if((down&HidNpadButton_Down)&&selected_model+1<model_count)selected_model++;if(down&HidNpadButton_B)current_view=VIEW_SETTINGS;if((down&HidNpadButton_A)&&model_count){snprintf(config.model,sizeof(config.model),"%s",models[selected_model]);save_config();snprintf(status_text,sizeof(status_text),"已选择模型：%s",config.model);current_view=VIEW_SETTINGS;}}
        Theme theme=get_theme();if(current_view==VIEW_CHAT)draw_chat(renderer,font,small,theme);else if(current_view==VIEW_SETTINGS)draw_settings(renderer,font,small,theme);else draw_models(renderer,font,small,theme);SDL_RenderPresent(renderer);
    }
    if(is_generating)finish_active_request(CURLE_ABORTED_BY_CALLBACK,1);else cleanup_active_request();
    save_config();save_sessions();destroy_bubble_cache();cleanup_image_cache();TTF_CloseFont(font);TTF_CloseFont(small);if(symbol)TTF_CloseFont(symbol);if(emoji)TTF_CloseFont(emoji);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);TTF_Quit();SDL_Quit();curl_global_cleanup();socketExit();romfsExit();fsdevUnmountAll();return 0;
}
