/* ============================================================================================ */
/* This software is created by John Anthony and comes with no warranty of any kind.             */
/*                                                                                              */
/* If you like this software and would like to contribute to its continued improvement          */
/* then please feel free to submit bug reports here: www.github.com/JohnAnthony                 */
/*                                                                                              */
/* This program is licensed under the GPLv3 and in support of Free and Open Source              */
/* Software in general. The full license can be found at http://www.gnu.org/licenses/gpl.html   */
/* ============================================================================================ */
#include <SDL/SDL.h>
#include <SDL/SDL_image.h>
#include <SDL/SDL_mixer.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#ifdef XINERAMA
#include <X11/Xlib.h>
#include <X11/extensions/Xinerama.h>
#endif /* XINERAMA */
#include "list.h" /* Linked list implementation */

#define BUF_SZ  1024

/* Type definitions */
typedef struct {
    int x, y;
} coords;

typedef struct cat_instance cat_instance;
struct cat_instance {
    coords loc;
    struct list_head list;
};

typedef struct sparkle_instance sparkle_instance;
struct sparkle_instance {
    unsigned int frame, speed;
    int frame_mov;
    unsigned int layer;
    coords loc;
    struct list_head list;
};

/* Predecs */
static void add_sparkle(void);
static void add_cat(unsigned int x, unsigned int y);
static void cleanup(void);
static void clear_screen(void);
static void draw_cats(unsigned int frame);
static void draw_sparkles(void);
static void* ec_malloc(unsigned int size);
static void errout(char *str);
static void fillsquare(SDL_Surface* surf, int x, int y, int w, int h, Uint32 col);
static void handle_args(int argc, char** argv);
static void handle_input(void);
static void init(void);
static void load_images(void);
static SDL_Surface* load_image(const char* path);
static void load_resource_data(void);
static void load_music(void);
static void putpix(SDL_Surface* surf, int x, int y, Uint32 col);
static void restart_music(void);
static void run(void);
static void stretch_images(void);
static void update_sparkles(void);
static void usage(char* exname);
#ifdef XINERAMA
static void xinerama_add_cats(void);
#endif /* XINERAMA */

/* Globals */
static unsigned int                 FRAMERATE = 14;
static unsigned int                 SCREEN_BPP = 32;
static unsigned int                 SCREEN_WIDTH = 800;
static unsigned int                 SCREEN_HEIGHT = 600;
static SDL_Surface*                 screen = NULL;
static SDL_Event                    event;
static int                          running = 1;
static int                          SURF_TYPE = SDL_HWSURFACE;
static int                          sound = 1;
static int                          sound_volume = 128;
static int                          fullscreen = 1;
static int                          catsize = 0;
static int                          cursor = 0;
#ifdef XINERAMA
static Display*                     dpy;
#endif /* XINERAMA */
static int                          curr_frame = 0;
static int                          sparkle_spawn_counter = 0;
static Mix_Music*                   music;
static SDL_Surface**                cat_img;
static SDL_Surface**                sparkle_img;
static SDL_Surface**                stretch_cat;
static SDL_Surface**                image_set;
static Uint32                       bgcolor;
static char*                        RESOURCE_PATH = NULL;
static char*                        LOC_BASE_PATH = "res";
static char*                        OS_BASE_PATH = "/usr/share/nyancat";
static int                          ANIM_FRAMES_FG = 0;
static int                          ANIM_FRAMES_BG = 0;
static LIST_HEAD(sparkle_list);
static LIST_HEAD(cat_list);

/* Function definitions */
static void
add_sparkle(void) {
    sparkle_instance* new;

    new = ec_malloc(sizeof(sparkle_instance));
    new->loc.x = screen->w + 80;
    new->loc.y = (rand() % (screen->h + sparkle_img[0]->h)) - sparkle_img[0]->h;
    new->frame = 0;
    new->frame_mov = 1;
    new->speed = 10 + (rand() % 30);
    new->layer = rand() % 2;
    list_add(&new->list, &sparkle_list);
}

static void
add_cat(unsigned int x, unsigned int y) {
    cat_instance* new;

    new = ec_malloc(sizeof(cat_instance));
    new->loc.x = x;
    new->loc.y = y;
    list_add(&new->list, &cat_list);
}

static void
cleanup(void) {
    Mix_HaltMusic();
    Mix_FreeMusic(music);
    Mix_CloseAudio();
    SDL_Quit();
}

static void
clear_screen(void) {
    sparkle_instance *s;
    cat_instance *c;

    list_for_each_entry(c, &cat_list, list) {
        /* This is bad. These magic numbers are to make up for uneven image sizes */
        fillsquare(screen,
                   c->loc.x,
                   c->loc.y - (curr_frame < 2 ? 0 : 5),
                   image_set[curr_frame]->w + 6,
                   image_set[curr_frame]->h + 5,
                   bgcolor);
    }

    list_for_each_entry(s, &sparkle_list, list) {
        fillsquare(screen,
                   s->loc.x,
                   s->loc.y,
                   sparkle_img[s->frame]->w,
                   sparkle_img[s->frame]->h,
                   bgcolor);
    }

}

static void
draw_cats(unsigned int frame) {
    cat_instance* c;
    SDL_Rect pos;

    list_for_each_entry(c, &cat_list, list) {
        pos.x = c->loc.x;
        pos.y = c->loc.y;

        if(frame < 2)
            pos.y -= 5;
        SDL_BlitSurface( image_set[frame], NULL, screen, &pos );
    }
}

static void
draw_sparkles() {
    sparkle_instance* s;
    SDL_Rect pos;

    list_for_each_entry(s, &sparkle_list, list) {
        pos.x = s->loc.x;
        pos.y = s->loc.y;
        SDL_BlitSurface( sparkle_img[s->frame], NULL, screen, &pos );
    }
}

static void*
ec_malloc(unsigned int size) {
    void *ptr;
    ptr = malloc(size);
    if (!ptr)
        errout("In ec_malloc -- unable to allocate memory.");
    return ptr;
}

static void
errout (char *str) {
    if (str)
        puts(str);
    exit(-1);
}

static void
fillsquare(SDL_Surface* surf, int x, int y, int w, int h, Uint32 col) {
    int i, e;

    if (x + w < 0 || y + h < 0 || x > surf->w || y > surf->h)
        return;

    /* Sanitising of inputs. Make sure we're not drawing off of the surface */
    if (x + w > surf->w)
        w = surf->w - x;
    if (y + h > surf->h)
        h = surf->h - y;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }

    for (i = x; i < x + w; i++)
        for (e = y; e < y + h; e++)
            putpix(surf, i, e, col);
}

static void
handle_args(int argc, char **argv) {
    int i;

    /* This REALLY needs to be replaced with getopt */

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-hw"))
            SURF_TYPE = SDL_HWSURFACE;
        else if (!strcmp(argv[i], "-sw"))
            SURF_TYPE = SDL_SWSURFACE;
        else if (!strcmp(argv[i], "-f") || !strcmp(argv[i], "--fullscreen"))
            fullscreen = 1;
        else if(!(strcmp(argv[i], "-nf") || !strcmp(argv[i], "--nofullscreen")))
            fullscreen = 0;
        else if(!strcmp(argv[i], "-nc") || !strcmp(argv[i], "--nocursor"))
            cursor = 0;
        else if(!strcmp(argv[i], "-sc") || !strcmp(argv[i], "--cursor") || !strcmp(argv[i], "--showcursor"))
            cursor = 1;
        else if(!strcmp(argv[i], "-ns") || !strcmp(argv[i], "--nosound"))
            sound = 0;
        else if((!strcmp(argv[i], "-v") || !strcmp(argv[i], "--volume")) && i < argc - 1) {
            int vol = atoi(argv[++i]);
            if(vol >= 0 && vol <= 128){
                sound_volume = vol;
            }
            else {
                puts("Arguments for Volume are not valid. Disabling sound.");
                sound = 0;
            }
        }
        else if(!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) 
            usage(argv[0]);
        else if(!strcmp(argv[i], "-c") || !strcmp(argv[i], "--catsize")) {
            if (++i < argc) {
                if(!strcmp(argv[i], "full"))
                    catsize = 1;
                else if(!strcmp(argv[i], "small"))
                    catsize = 0;
                else
                    printf("Unrecognised scaling option: %s - please select either 'full' or 'small' cat size.\n", argv[i]);
            }
        }
        else if(!strcmp(argv[i], "-d") || !strcmp(argv[i], "--data-set")) {
            if (++i < argc) {
                if (RESOURCE_PATH)
                    free(RESOURCE_PATH);
                RESOURCE_PATH = strdup(argv[i]);
            }
        }
        else if((!strcmp(argv[i], "-r") && strcmp(argv[i], "--resolution")) && i < argc - 2) {
            int dims[2] = { atoi(argv[++i]), atoi(argv[++i]) };
            if (dims[0] >= 0 && dims[0] < 10000 && dims[1] >= 0 && dims[1] < 5000) {           // Borrowed from PixelUnsticker, changed the variable name
                SCREEN_WIDTH = dims[0];
                SCREEN_HEIGHT = dims[1];
            }
            else
                puts("Arguments do not appear to be valid screen sizes. Defaulting.");
        }
        else
            printf("Unrecognised option: %s\n", argv[i]);
    }

    if (!RESOURCE_PATH)
        RESOURCE_PATH = "default";
}

static void
handle_input(void) {
    while( SDL_PollEvent( &event ) ) {
        switch (event.type) {
            case SDL_KEYDOWN:
            case SDL_QUIT:
            case SDL_MOUSEMOTION:
                running = 0;
                break;
        }
    }
}

static void
init(void) {
    int i;

    srand( time(NULL) );

    SDL_Init( SDL_INIT_EVERYTHING );
    if (fullscreen)
        screen = SDL_SetVideoMode( 0, 0, SCREEN_BPP, SURF_TYPE | SDL_FULLSCREEN );
    else
        screen = SDL_SetVideoMode( SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_BPP, SURF_TYPE );
    if(!cursor)
        SDL_ShowCursor(0);

    load_resource_data();
    load_images();
    bgcolor = SDL_MapRGB(screen->format, 0x00, 0x33, 0x66);
    fillsquare(screen, 0, 0, screen->w, screen->h, bgcolor);

    if(sound) {
        Mix_OpenAudio( 44100, AUDIO_S16, 2, 256 );
        load_music();
        Mix_PlayMusic(music, 0);
        Mix_VolumeMusic(sound_volume);
    }

    /* Choose our image set */
    if (catsize == 1)
        image_set = stretch_cat;
    else
        image_set = cat_img;

/* Ugly */
#ifdef XINERAMA
    if (!(dpy = XOpenDisplay(NULL)))
        puts("Failed to open Xinerama display information.");
    else{
        if(catsize == 1)
            stretch_images();
        xinerama_add_cats();
        XCloseDisplay(dpy);
    }
#else
    if(catsize == 1)
        add_cat(0, (screen->h - image_set[0]->h) / 2);
    else {
        add_cat((screen->w - cat_img[0]->w) / 2, (screen->h - cat_img[0]->h) / 2);
    }
#endif /* Xinerama */

    /* clear initial input */
    while( SDL_PollEvent( &event ) ) {}

    /* Pre-populate with sparkles */
    for (i = 0; i < 200; i++)
        update_sparkles();
}

static void
load_images(void) {
    int i;
    char buffer[BUF_SZ];

    cat_img = ec_malloc(sizeof(SDL_Surface*) * ANIM_FRAMES_FG);
    sparkle_img = ec_malloc(sizeof(SDL_Surface*) * ANIM_FRAMES_BG);

    /* Loading logic */
    for (i = 0; i < ANIM_FRAMES_FG; ++i) {
        snprintf(buffer, BUF_SZ, "%s/%s/fg%02d.png", LOC_BASE_PATH, RESOURCE_PATH, i);
        cat_img[i] = load_image(buffer);
        if (!cat_img[i]) {
            snprintf(buffer, BUF_SZ, "%s/%s/fg%02d.png", OS_BASE_PATH, RESOURCE_PATH, i);
            cat_img[i] = load_image(buffer);
        }
    }
    for (i = 0; i < ANIM_FRAMES_BG; ++i) {
        snprintf(buffer, BUF_SZ, "%s/%s/bg%02d.png", LOC_BASE_PATH, RESOURCE_PATH, i);
        sparkle_img[i] = load_image(buffer);
        if (!sparkle_img[i]) {
            snprintf(buffer, BUF_SZ, "%s/%s/bg%02d.png", OS_BASE_PATH, RESOURCE_PATH, i);
            sparkle_img[i] = load_image(buffer);
        }
    }

    /* Check everything loaded properly */
    for (int i = 0; i < ANIM_FRAMES_FG; ++i)
        if (!cat_img[i])
            errout("Error loading foreground images.");

    for (int i = 0; i < ANIM_FRAMES_BG; ++i)
        if (!sparkle_img[i])
            errout("Error loading background images.");
}

static SDL_Surface*
load_image( const char* path ) {
    SDL_Surface* loadedImage = NULL;
    SDL_Surface* optimizedImage = NULL;

    loadedImage = IMG_Load( path );
    if(loadedImage) {
        optimizedImage = SDL_DisplayFormatAlpha( loadedImage );
        SDL_FreeSurface( loadedImage );
    }
    return optimizedImage;
}

static void
load_music(void) {
    char buffer[BUF_SZ];

    snprintf(buffer, BUF_SZ, "%s/%s/music.ogg", LOC_BASE_PATH, RESOURCE_PATH);
    music = Mix_LoadMUS(buffer);
    if (!music) {
        snprintf(buffer, BUF_SZ, "%s/%s/music.ogg", OS_BASE_PATH, RESOURCE_PATH);
        music = Mix_LoadMUS(buffer);
    }
    if (!music)
        printf("Unable to load Ogg file: %s\n", Mix_GetError());
    else
        Mix_HookMusicFinished(restart_music);
}

static void
load_resource_data(void) {
    FILE *f;
    char buffer[BUF_SZ];

    snprintf(buffer, BUF_SZ, "%s/%s/data", LOC_BASE_PATH, RESOURCE_PATH);
    f = fopen(buffer, "r");
    if (!f) {
        snprintf(buffer, BUF_SZ, "%s/%s/data", OS_BASE_PATH, RESOURCE_PATH);
        f = fopen(buffer, "r");
    }
    if (!f)
        errout("Error opening resource data file");

    ANIM_FRAMES_FG = atoi(fgets(buffer, BUF_SZ, f));
    ANIM_FRAMES_BG = atoi(fgets(buffer, BUF_SZ, f));

    if (!ANIM_FRAMES_FG || !ANIM_FRAMES_BG)
        errout("Error reading resource data file.");
}

static void
putpix(SDL_Surface* surf, int x, int y, Uint32 col) {
    Uint32 *pix = (Uint32 *) surf->pixels;
    pix [ ( y * surf->w ) + x ] = col;
}

static void
restart_music(void) {
    Mix_PlayMusic(music, 0);
}

static void
run(void) {
    unsigned int last_draw, draw_time;

    while( running ) {
        last_draw = SDL_GetTicks();

        clear_screen();
        update_sparkles();
        draw_sparkles();
        draw_cats(curr_frame);

        handle_input();
        SDL_Flip(screen);

        /* Frame increment and looping */
        curr_frame++;
        if (curr_frame >= ANIM_FRAMES_FG)
            curr_frame = 0;

        draw_time = SDL_GetTicks() - last_draw;
        if (draw_time < (1000 / FRAMERATE))
            SDL_Delay((1000 / FRAMERATE) - draw_time);
    }
}

static void
stretch_images(void) {
    SDL_Rect stretchto;
    stretchto.w = 0;
    stretchto.h = 0;

    /*  Just use the x co-ordinate for scaling for now. This does, however,
        need to be changed to accomodate taller resolutions */
#ifdef XINERAMA
    int i, nn;
    XineramaScreenInfo* info = XineramaQueryScreens(dpy, &nn);

    for (i = 0; i < nn; ++i) {
        if(!stretchto.w || info[i].width < stretchto.w)
            stretchto.w = info[i].width;
    }

    XFree(info);
#endif /* XINERAMA */
    if (!stretchto.w)
        stretchto.w = screen->w;

    /* Handle a slight scaling down */
    stretchto.w *= 0.9;
    stretchto.h = stretchto.w * cat_img[0]->h / cat_img[0]->w;

    SDL_PixelFormat fmt = *(cat_img[0]->format);
    for (int i=0; i <= ANIM_FRAMES_FG; i++) {
        stretch_cat[i] = SDL_CreateRGBSurface(SURF_TYPE, stretchto.w,
            stretchto.h,SCREEN_BPP,fmt.Rmask,fmt.Gmask,fmt.Bmask,fmt.Amask);
        SDL_SoftStretch(cat_img[i],NULL,stretch_cat[i],NULL);
    }

}

static void
update_sparkles(void) {
    sparkle_instance *s;
    sparkle_instance *tmp;

    sparkle_spawn_counter += rand() % screen->h;
    while(sparkle_spawn_counter >= 1000) {
        add_sparkle();
        sparkle_spawn_counter -= 1000;
    }

    list_for_each_entry_safe(s, tmp, &sparkle_list, list) {
        s->loc.x -= s->speed;
        s->frame += s->frame_mov;

        if(s->frame + 1 >= ANIM_FRAMES_BG || s->frame < 1)
            s->frame_mov = 0 - s->frame_mov;

        if (s->loc.x < 0 - sparkle_img[0]->w) {
            list_del(&s->list);
            free(s);
        }
    }
}

static void
usage(char* exname) {
    printf("Usage: %s [OPTIONS]\n\
    -h,  --help                    This help message\n\
    -f,  --fullscreen              Enable fullscreen mode (default)\n\
    -nf, --nofullscreen            Disable fullscreen mode\n\
    -c,  --catsize                 Choose size of cat, options are full and \n\
                                   small. Small is default. \"Full\" not\n\
                                   officially supported.\n\
    -nc, --nocursor                Don't show the cursor (default)\n\
    -sc, --cursor, --showcursor    Show the cursor\n\
    -ns, --nosound                 Don't play sound\n\
    -v,  --volume                  Set Volume, if enabled, from 0 - 128\n\
    -r,  --resolution              Make next two arguments the screen \n\
                                   resolution to use (0 and 0 for full \n\
                                   resolution) (800x600 default)\n\
    -d, --data-set                 Use an alternate data set. Packaged with\n\
                                   this program by default are \"default\"\n\
                                   and \"freedom\" sets.\n\
    -hw, -sw                       Use hardware or software SDL rendering, \n\
                                   respectively. Hardware is default\n", exname);
    exit(0);
}

#ifdef XINERAMA
static void
xinerama_add_cats(void) {
    int i, nn;
    XineramaScreenInfo* info = XineramaQueryScreens(dpy, &nn);

    for (i = 0; i < nn; ++i) {
        if(fullscreen) {
            add_cat(info[i].x_org + ((info[i].width - image_set[0]->w) / 2),
                info[i].y_org + ((info[i].height - image_set[0]->h) / 2));
        }
        else {
            add_cat((SCREEN_WIDTH - image_set[0]->w) / 2, 
                (SCREEN_HEIGHT - image_set[0]->h) / 2);
        }
    }

    XFree(info);
}
#endif /* XINERAMA */

int main( int argc, char **argv ) {
    handle_args(argc, argv);
    init();
    run();
    cleanup();
    return 0;
}
