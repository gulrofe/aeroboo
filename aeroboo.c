// aeroboo.c
//
// "aeroboo" with splash, entry screen, and game in a single executable.
// Requires SDL2, SDL2_image and SDL2_mixer.

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_mixer.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// Window dimensions
#define WIN_W 800
#define WIN_H 600

// Splash screen
#define SPLASH_IMG         "splash.png"
#define SPLASH_DURATION_MS 3000  // 3 seconds

// Entry screen
#define ENTRY_IMG          "urubu.png"
#define ENTRY_MUSIC_FILE   "stay-with-me-384602.mp3"

// Sprite background removal thresholds
#define DESBLEND_LOW   8
#define DESBLEND_HIGH 40

// Cannon and projectile configuration
#define CANNON_FRAMES        3
const double CANNON_FRAME_TIME = 0.12;
const float  PROJECTILE_SPEED  = 600.0f;
const double EXPLOSION_TIME    = 0.5;
const double WINNER_TIME       = 2.0;

// Clamp integer into Uint8 range [0,255]
static Uint8 clamp_u8(int v) {
    if (v <   0) return   0;
    if (v > 255) return 255;
    return (Uint8)v;
}

// Convert any surface to RGBA32 format
static SDL_Surface* convert_to_rgba32(SDL_Surface* s) {
    if (!s) return NULL;
    return SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_RGBA32, 0);
}

// Sample the average backdrop color from the four corners
static void sample_corners_color(SDL_Surface* s,
                                 Uint8 *or_, Uint8 *og, Uint8 *ob) {
    if (!s || !or_ || !og || !ob) return;
    if (SDL_MUSTLOCK(s)) SDL_LockSurface(s);

    Uint32 *pix = (Uint32*)s->pixels;
    int w = s->w, h = s->h;
    long sr=0, sg=0, sb=0;
    SDL_PixelFormat* fmt = s->format;
    Uint8 r,g,b,a;

    Uint32 corners[4] = {
        pix[0],
        pix[w-1],
        pix[(h-1)*w + 0],
        pix[(h-1)*w + (w-1)]
    };
    for(int i=0; i<4; i++){
        SDL_GetRGBA(corners[i], fmt, &r, &g, &b, &a);
        sr += r; sg += g; sb += b;
    }

    if (SDL_MUSTLOCK(s)) SDL_UnlockSurface(s);
    *or_ = (Uint8)(sr/4);
    *og  = (Uint8)(sg/4);
    *ob  = (Uint8)(sb/4);
                                 }

                                 // Automatically generate sprite alpha channel by removing backdrop
                                 static void make_sprite_from_bg(SDL_Surface* s, int low, int high) {
                                     if (!s) return;
                                     if (s->format->format != SDL_PIXELFORMAT_RGBA32) return;
                                     if (SDL_MUSTLOCK(s)) SDL_LockSurface(s);

                                     Uint8 *B = (Uint8*)s->pixels;
                                     int w = s->w, h = s->h;
                                     Uint8 bg_r, bg_g, bg_b;
                                     sample_corners_color(s, &bg_r, &bg_g, &bg_b);

                                     for(int i=0, tot=w*h; i<tot; i++){
                                         Uint8 r = B[4*i+0], g = B[4*i+1], b = B[4*i+2];
                                         int dr = abs(r - bg_r),
                                         dg = abs(g - bg_g),
                                         db = abs(b - bg_b);
                                         int d = dr>dg?dr:dg; d = db>d?db:d;
                                         float a = (d <= low
                                         ? 0.0f
                                         : (d >= high
                                         ? 1.0f
                                         : (float)(d-low)/(high-low)));
                                         Uint8 outA = clamp_u8((int)(255*a + .5f));
                                         Uint8 outR, outG, outB;
                                         if (a <= 0.0f) {
                                             outR = bg_r; outG = bg_g; outB = bg_b;
                                         } else {
                                             outR = clamp_u8((int)(((r - (1.f - a)*bg_r)/a) + .5f));
                                             outG = clamp_u8((int)(((g - (1.f - a)*bg_g)/a) + .5f));
                                             outB = clamp_u8((int)(((b - (1.f - a)*bg_b)/a) + .5f));
                                         }
                                         Uint32 p = SDL_MapRGBA(s->format, outR, outG, outB, outA);
                                         ((Uint32*)B)[i] = p;
                                     }

                                     if (SDL_MUSTLOCK(s)) SDL_UnlockSurface(s);
                                 }

                                 // Load a texture from file and process its sprite backdrop removal
                                 static SDL_Texture* load_and_process_texture(SDL_Renderer* ren,
                                                                              const char* path,
                                                                              SDL_Surface **out_surf) {
                                     *out_surf = NULL;
                                     SDL_Surface* orig = IMG_Load(path);
                                     if (!orig) {
                                         fprintf(stderr, "IMG_Load('%s'): %s\n", path, IMG_GetError());
                                         return NULL;
                                     }
                                     SDL_Surface* s32 = convert_to_rgba32(orig);
                                     SDL_FreeSurface(orig);
                                     if (!s32) return NULL;

                                     SDL_Surface* sample = SDL_ConvertSurfaceFormat(s32,
                                                                                    SDL_PIXELFORMAT_RGBA32, 0);
                                     *out_surf = sample ? sample : s32;

                                     make_sprite_from_bg(s32, DESBLEND_LOW, DESBLEND_HIGH);

                                     SDL_Texture* tex = SDL_CreateTextureFromSurface(ren, s32);
                                     SDL_FreeSurface(s32);
                                     if (!tex) {
                                         fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
                                         if (sample) SDL_FreeSurface(sample);
                                         return NULL;
                                     }
                                     SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
                                     return tex;
                                                                              }

                                                                              // Check intersection of two floating-point rectangles
                                                                              static int rects_intersectf(float ax, float ay, float aw, float ah,
                                                                                                          float bx, float by, float bw, float bh) {
                                                                                  if (ax + aw <= bx) return 0;
                                                                                  if (bx + bw <= ax) return 0;
                                                                                  if (ay + ah <= by) return 0;
                                                                                  if (by + bh <= ay) return 0;
                                                                                  return 1;
                                                                                                          }

                                                                                                          // Display the splash screen in an isolated SDL context, then quit it
                                                                                                          static void show_splash(void) {
                                                                                                              if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
                                                                                                                  fprintf(stderr, "SDL_Init(splash): %s\n", SDL_GetError());
                                                                                                                  return;
                                                                                                              }
                                                                                                              if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG)) {
                                                                                                                  fprintf(stderr, "IMG_Init(splash): %s\n", IMG_GetError());
                                                                                                                  SDL_Quit();
                                                                                                                  return;
                                                                                                              }

                                                                                                              SDL_Window   *sw = SDL_CreateWindow("Aeroboo",
                                                                                                                                                  SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                                                                                                                                  WIN_W, WIN_H, 0);
                                                                                                              SDL_Renderer *sr = SDL_CreateRenderer(sw, -1,
                                                                                                                                                    SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
                                                                                                              if (sw && sr) {
                                                                                                                  SDL_Texture *st = IMG_LoadTexture(sr, SPLASH_IMG);
                                                                                                                  if (st) {
                                                                                                                      Uint32 start = SDL_GetTicks();
                                                                                                                      while (SDL_GetTicks() - start < SPLASH_DURATION_MS) {
                                                                                                                          SDL_Event e;
                                                                                                                          while (SDL_PollEvent(&e))
                                                                                                                              if (e.type == SDL_QUIT) {
                                                                                                                                  start = SDL_GetTicks() + SPLASH_DURATION_MS;
                                                                                                                                  break;
                                                                                                                              }
                                                                                                                              SDL_RenderClear(sr);
                                                                                                                          SDL_RenderCopy(sr, st, NULL, NULL);
                                                                                                                          SDL_RenderPresent(sr);
                                                                                                                          SDL_Delay(16);
                                                                                                                      }
                                                                                                                      SDL_DestroyTexture(st);
                                                                                                                  }
                                                                                                              }

                                                                                                              if (sr) SDL_DestroyRenderer(sr);
                                                                                                              if (sw) SDL_DestroyWindow(sw);
                                                                                                              IMG_Quit();
                                                                                                              SDL_Quit();
                                                                                                          }

                                                                                                          int main(int argc, char **argv) {
                                                                                                              (void)argc; (void)argv;

                                                                                                              // 1) Display the splash and shut down its SDL/IMG subsystems
                                                                                                              show_splash();

                                                                                                              // 2) Initialize SDL for video, timers, and audio
                                                                                                              if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO) != 0) {
                                                                                                                  fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
                                                                                                                  return 1;
                                                                                                              }
                                                                                                              if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG)) {
                                                                                                                  fprintf(stderr, "IMG_Init: %s\n", IMG_GetError());
                                                                                                                  SDL_Quit();
                                                                                                                  return 1;
                                                                                                              }
                                                                                                              int mix_flags = MIX_INIT_MP3;
                                                                                                              if ((Mix_Init(mix_flags) & mix_flags) != mix_flags) {
                                                                                                                  fprintf(stderr, "Warning: Mix_Init MP3: %s\n", Mix_GetError());
                                                                                                              }
                                                                                                              if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
                                                                                                                  fprintf(stderr, "Mix_OpenAudio: %s\n", Mix_GetError());
                                                                                                              }

                                                                                                              // 3) Create the main window and renderer for entry and game
                                                                                                              SDL_Window   *win = SDL_CreateWindow("Aeroboo",
                                                                                                                                                   SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                                                                                                                                   WIN_W, WIN_H, 0);
                                                                                                              SDL_Renderer *ren = SDL_CreateRenderer(win, -1,
                                                                                                                                                     SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
                                                                                                              if (!win || !ren) {
                                                                                                                  fprintf(stderr, "Window/Ren error: %s\n", SDL_GetError());
                                                                                                                  goto CLEANUP;
                                                                                                              }

                                                                                                              // 4) Entry screen: load background image and mascot sprite, start music
                                                                                                              SDL_Texture *entry_bg   = IMG_LoadTexture(ren, SPLASH_IMG);
                                                                                                              SDL_Texture *entry_masc = IMG_LoadTexture(ren, ENTRY_IMG);
                                                                                                              Mix_Music   *entry_mus  = Mix_LoadMUS(ENTRY_MUSIC_FILE);
                                                                                                              if (!entry_bg || !entry_masc) {
                                                                                                                  fprintf(stderr, "Error: failed to load entry assets (splash/vulture)\n");
                                                                                                                  // Will skip directly into the game if missing
                                                                                                              }

                                                                                                              // Center the entry mascot
                                                                                                              SDL_Rect dstMas = {0};
                                                                                                              if (entry_masc) {
                                                                                                                  int w, h;
                                                                                                                  SDL_QueryTexture(entry_masc, NULL, NULL, &w, &h);
                                                                                                                  dstMas.w = w; dstMas.h = h;
                                                                                                                  dstMas.x = (WIN_W - w) / 2;
                                                                                                                  dstMas.y = (WIN_H - h) / 2;
                                                                                                              }

                                                                                                              // Play entry music in loop
                                                                                                              if (entry_mus) Mix_PlayMusic(entry_mus, -1);

                                                                                                              // Wait for mouse click or quit event
                                                                                                              int entryRunning = 1, quitAll = 0;
                                                                                                              while (entryRunning) {
                                                                                                                  SDL_Event e;
                                                                                                                  while (SDL_PollEvent(&e)) {
                                                                                                                      if (e.type == SDL_QUIT) {
                                                                                                                          entryRunning = 0;
                                                                                                                          quitAll = 1;
                                                                                                                      }
                                                                                                                      if (e.type == SDL_MOUSEBUTTONDOWN) {
                                                                                                                          entryRunning = 0;
                                                                                                                      }
                                                                                                                  }
                                                                                                                  SDL_RenderClear(ren);
                                                                                                                  if (entry_bg)   SDL_RenderCopy(ren, entry_bg,   NULL, NULL);
                                                                                                                  if (entry_masc) SDL_RenderCopy(ren, entry_masc, NULL, &dstMas);
                                                                                                                  SDL_RenderPresent(ren);
                                                                                                                  SDL_Delay(16);
                                                                                                              }

                                                                                                              // Stop music and free entry resources
                                                                                                              Mix_HaltMusic();
                                                                                                              if (entry_mus)  Mix_FreeMusic(entry_mus);
                                                                                                              if (entry_bg)   SDL_DestroyTexture(entry_bg);
                                                                                                              if (entry_masc) SDL_DestroyTexture(entry_masc);

                                                                                                              if (quitAll) {
                                                                                                                  // Clean up and exit before the game starts
                                                                                                                  goto CLEANUP;
                                                                                                              }

                                                                                                              // 5) Prepare for the main game loop: load assets and initialize state

                                                                                                              // Music and sound effects
                                                                                                              Mix_Music *music_vulture = Mix_LoadMUS("vulture.mp3");
                                                                                                              if (!music_vulture) {
                                                                                                                  music_vulture = Mix_LoadMUS("vulture.wav");
                                                                                                              }
                                                                                                              Mix_Chunk *sfx_canon     = Mix_LoadWAV("canon.mp3");
                                                                                                              if (!sfx_canon)     sfx_canon     = Mix_LoadWAV("canon.wav");
                                                                                                              Mix_Chunk *sfx_explosion = Mix_LoadWAV("explosion.mp3");
                                                                                                              if (!sfx_explosion) sfx_explosion = Mix_LoadWAV("explosion.wav");
                                                                                                              Mix_Chunk *sfx_winner    = Mix_LoadWAV("win.mp3");
                                                                                                              if (!sfx_winner) {
                                                                                                                  sfx_winner = Mix_LoadWAV("win.wav");
                                                                                                                  if (!sfx_winner) {
                                                                                                                      fprintf(stderr, "Warning: failed to load win.mp3/.wav: %s\n", Mix_GetError());
                                                                                                                  }
                                                                                                              }

                                                                                                              // Bird sprites
                                                                                                              SDL_Surface *s_bu1 = NULL, *s_bu2 = NULL;
                                                                                                              SDL_Texture *t_bu1 = load_and_process_texture(ren, "bu1.png", &s_bu1);
                                                                                                              SDL_Texture *t_bu2 = load_and_process_texture(ren, "bu2.png", &s_bu2);

                                                                                                              // Cannon frames
                                                                                                              SDL_Surface *s_c[CANNON_FRAMES] = {0};
                                                                                                              SDL_Texture *t_c[CANNON_FRAMES] = {0};
                                                                                                              const char* cf[CANNON_FRAMES] = {"cano1.png","cano2.png","cano3.png"};
                                                                                                              for (int i = 0; i < CANNON_FRAMES; i++) {
                                                                                                                  t_c[i] = load_and_process_texture(ren, cf[i], &s_c[i]);
                                                                                                              }

                                                                                                              // Explosion and winner images
                                                                                                              SDL_Surface *s_exp = NULL, *s_win = NULL;
                                                                                                              SDL_Texture *t_exp = load_and_process_texture(ren, "explosion.png", &s_exp);
                                                                                                              SDL_Texture *t_win = load_and_process_texture(ren, "winner.png", &s_win);
                                                                                                              int win_w = 0, win_h = 0;
                                                                                                              if (t_win) SDL_QueryTexture(t_win, NULL, NULL, &win_w, &win_h);

                                                                                                              // Fallback background color if corner sampling fails
                                                                                                              const Uint8 bg_fallback[3] = {135,206,235};
                                                                                                              Uint8 bg_r = bg_fallback[0],
                                                                                                              bg_g = bg_fallback[1],
                                                                                                              bg_b = bg_fallback[2];

                                                                                                              // Bird animation parameters
                                                                                                              int bw, bh;
                                                                                                              SDL_QueryTexture(t_bu1, NULL, NULL, &bw, &bh);
                                                                                                              float bs = 128.f / (float)bw;
                                                                                                              bw = (int)(bw * bs + .5f);
                                                                                                              bh = (int)(bh * bs + .5f);
                                                                                                              float bird_x     = WIN_W + 10, bird_y = 12;
                                                                                                              float bird_vx    = 180.f;
                                                                                                              int   bird_frame = 0, bird_active = 1;
                                                                                                              const double bird_frametime = 0.18;
                                                                                                              double bird_acc = 0.0;

                                                                                                              // Cannon positioning and animation state
                                                                                                              int cw, ch;
                                                                                                              SDL_QueryTexture(t_c[0], NULL, NULL, &cw, &ch);
                                                                                                              float cs = (192.f / (float)cw) * 0.5f;
                                                                                                              cw = (int)(cw * cs + .5f);
                                                                                                              ch = (int)(ch * cs + .5f);
                                                                                                              float cx = (WIN_W - cw) / 2.f,
                                                                                                              cy = WIN_H - ch - 8;
                                                                                                              int canon_play  = 0, canon_frame = 0, proj_spawn = 0;
                                                                                                              double canon_acc = 0.0;

                                                                                                              // Projectile state
                                                                                                              int   proj_active = 0;
                                                                                                              float proj_x = 0, proj_y = 0;
                                                                                                              const int pw = 10, ph = 10;

                                                                                                              // Explosion state
                                                                                                              int   exp_active = 0;
                                                                                                              float exp_x = 0, exp_y = 0;
                                                                                                              double exp_timer = 0.0;

                                                                                                              // Winner display state
                                                                                                              int    winner_active = 0;
                                                                                                              double winner_timer  = 0.0;

                                                                                                              // Timing setup
                                                                                                              Uint64 last = SDL_GetPerformanceCounter();
                                                                                                              double freq = (double)SDL_GetPerformanceFrequency();
                                                                                                              int running = 1, paused = 0;

                                                                                                              // Start vulture music if bird is active
                                                                                                              if (music_vulture && bird_active && !exp_active) {
                                                                                                                  Mix_PlayMusic(music_vulture, -1);
                                                                                                                  Mix_VolumeMusic(MIX_MAX_VOLUME * 60 / 100);
                                                                                                              }

                                                                                                              // 6) Main game loop
                                                                                                              while (running) {
                                                                                                                  SDL_Event e;
                                                                                                                  while (SDL_PollEvent(&e)) {
                                                                                                                      if (e.type == SDL_QUIT) {
                                                                                                                          running = 0;
                                                                                                                      }
                                                                                                                      else if (e.type == SDL_KEYDOWN) {
                                                                                                                          if (e.key.keysym.sym == SDLK_ESCAPE) running = 0;
                                                                                                                          if (e.key.keysym.sym == SDLK_SPACE)  paused = !paused;
                                                                                                                      }
                                                                                                                      else if (e.type == SDL_MOUSEBUTTONDOWN &&
                                                                                                                          e.button.button == SDL_BUTTON_LEFT) {
                                                                                                                          if (!canon_play) {
                                                                                                                              canon_play  = 1;
                                                                                                                              canon_acc   = 0.0;
                                                                                                                              canon_frame = 0;
                                                                                                                              proj_spawn  = 0;
                                                                                                                          }
                                                                                                                          }
                                                                                                                  }

                                                                                                                  Uint64 now = SDL_GetPerformanceCounter();
                                                                                                                  double dt  = (now - last) / freq;
                                                                                                                  last = now;

                                                                                                                  if (!paused) {
                                                                                                                      // Update bird position and frame
                                                                                                                      if (bird_active && !exp_active) {
                                                                                                                          bird_x -= bird_vx * (float)dt;
                                                                                                                          if (bird_x < -bw - 10) bird_x = WIN_W + 10;
                                                                                                                          bird_acc += dt;
                                                                                                                          if (bird_acc >= bird_frametime) {
                                                                                                                              bird_acc -= bird_frametime;
                                                                                                                              bird_frame ^= 1;
                                                                                                                          }
                                                                                                                      }

                                                                                                                      // Manage bird music playback
                                                                                                                      if (bird_active && !exp_active) {
                                                                                                                          if (music_vulture && Mix_PlayingMusic() == 0)
                                                                                                                              Mix_PlayMusic(music_vulture, -1);
                                                                                                                      } else {
                                                                                                                          if (Mix_PlayingMusic() != 0)
                                                                                                                              Mix_HaltMusic();
                                                                                                                      }

                                                                                                                      // Animate cannon firing
                                                                                                                      if (canon_play) {
                                                                                                                          canon_acc += dt;
                                                                                                                          if (canon_acc >= CANNON_FRAME_TIME) {
                                                                                                                              canon_acc  -= CANNON_FRAME_TIME;
                                                                                                                              canon_frame++;
                                                                                                                              if (canon_frame >= CANNON_FRAMES) {
                                                                                                                                  canon_play  = 0;
                                                                                                                                  canon_frame = 0;
                                                                                                                                  proj_spawn  = 0;
                                                                                                                              }
                                                                                                                          }
                                                                                                                          if (canon_frame == CANNON_FRAMES - 1 && !proj_spawn) {
                                                                                                                              proj_active = 1;
                                                                                                                              proj_x = cx + cw * 0.5f - pw * 0.5f;
                                                                                                                              proj_y = cy + ch * 0.15f;
                                                                                                                              proj_spawn = 1;
                                                                                                                              if (sfx_canon) Mix_PlayChannel(-1, sfx_canon, 0);
                                                                                                                          }
                                                                                                                      }

                                                                                                                      // Update projectile movement and collisions
                                                                                                                      if (proj_active) {
                                                                                                                          proj_y -= PROJECTILE_SPEED * (float)dt;
                                                                                                                          if (proj_y + ph < -50) proj_active = 0;
                                                                                                                          if (bird_active && !exp_active &&
                                                                                                                              rects_intersectf(proj_x, proj_y, pw, ph,
                                                                                                                                               bird_x, bird_y, bw, bh)) {
                                                                                                                              proj_active = 0;
                                                                                                                          exp_active  = 1;
                                                                                                                          exp_x       = bird_x;
                                                                                                                          exp_y       = bird_y;
                                                                                                                          exp_timer   = EXPLOSION_TIME;
                                                                                                                          bird_active = 0;
                                                                                                                          if (sfx_explosion) Mix_PlayChannel(-1, sfx_explosion, 0);
                                                                                                                          if (Mix_PlayingMusic() != 0) Mix_HaltMusic();

                                                                                                                          winner_active = 1;
                                                                                                                              winner_timer  = WINNER_TIME;
                                                                                                                              if (sfx_winner) Mix_PlayChannel(-1, sfx_winner, 0);
                                                                                                                                               }
                                                                                                                      }

                                                                                                                      // Countdown explosion duration
                                                                                                                      if (exp_active) {
                                                                                                                          exp_timer -= dt;
                                                                                                                          if (exp_timer <= 0.0) exp_active = 0;
                                                                                                                      }

                                                                                                                      // Countdown winner display duration
                                                                                                                      if (winner_active) {
                                                                                                                          winner_timer -= dt;
                                                                                                                          if (winner_timer <= 0.0) {
                                                                                                                              winner_active = 0;
                                                                                                                              bird_active   = 1;
                                                                                                                              bird_x        = WIN_W + 10;
                                                                                                                              bird_frame    = 0;
                                                                                                                              bird_acc      = 0.0;
                                                                                                                              if (music_vulture)
                                                                                                                                  Mix_PlayMusic(music_vulture, -1);
                                                                                                                          }
                                                                                                                      }
                                                                                                                  }

                                                                                                                  // Rendering
                                                                                                                  SDL_SetRenderDrawColor(ren, bg_r, bg_g, bg_b, 255);
                                                                                                                  SDL_RenderClear(ren);

                                                                                                                  if (winner_active && t_win) {
                                                                                                                      // Draw "WINNER" centered on screen
                                                                                                                      int dw = win_w, dh = win_h;
                                                                                                                      const int TW = 400;
                                                                                                                      if (win_w > TW) {
                                                                                                                          float sc = (float)TW / (float)win_w;
                                                                                                                          dw = (int)(win_w * sc + 0.5f);
                                                                                                                          dh = (int)(win_h * sc + 0.5f);
                                                                                                                      }
                                                                                                                      SDL_Rect rw = { (WIN_W - dw) / 2, (WIN_H - dh) / 2, dw, dh };
                                                                                                                      SDL_RenderCopy(ren, t_win, NULL, &rw);
                                                                                                                  }
                                                                                                                  else {
                                                                                                                      // Draw explosion, bird, or cannon + projectile
                                                                                                                      if (exp_active) {
                                                                                                                          SDL_Rect re = { (int)(exp_x + 0.5f),
                                                                                                                              (int)(exp_y + 0.5f),
                                                                                                                              bw, bh };
                                                                                                                              SDL_RenderCopy(ren, t_exp, NULL, &re);
                                                                                                                      }
                                                                                                                      else if (bird_active) {
                                                                                                                          SDL_Rect rb = { (int)(bird_x + 0.5f),
                                                                                                                              (int)(bird_y + 0.5f),
                                                                                                                              bw, bh };
                                                                                                                              SDL_RenderCopy(ren,
                                                                                                                                             (bird_frame == 0 ? t_bu1 : t_bu2),
                                                                                                                                             NULL, &rb);
                                                                                                                      }

                                                                                                                      int cfidx = (canon_play ? canon_frame : 0);
                                                                                                                      if (cfidx < 0) cfidx = 0;
                                                                                                                      if (cfidx >= CANNON_FRAMES) cfidx = CANNON_FRAMES - 1;
                                                                                                                      SDL_Rect rc = { (int)(cx + 0.5f),
                                                                                                                          (int)(cy + 0.5f),
                                                                                                                          cw, ch };
                                                                                                                          SDL_RenderCopy(ren, t_c[cfidx], NULL, &rc);

                                                                                                                          if (proj_active) {
                                                                                                                              SDL_Rect rp = {
                                                                                                                                  (int)(proj_x + 0.5f),
                                                                                                                                  (int)(proj_y + 0.5f),
                                                                                                                                  pw, ph
                                                                                                                              };
                                                                                                                              SDL_SetRenderDrawColor(ren, 220, 200, 60, 255);
                                                                                                                              SDL_RenderFillRect(ren, &rp);
                                                                                                                          }
                                                                                                                  }

                                                                                                                  SDL_RenderPresent(ren);
                                                                                                                  SDL_Delay(16);
                                                                                                              }

                                                                                                              CLEANUP:
                                                                                                              // Free audio resources
                                                                                                              if (music_vulture)   Mix_FreeMusic(music_vulture);
                                                                                                              if (sfx_canon)       Mix_FreeChunk(sfx_canon);
                                                                                                              if (sfx_explosion)   Mix_FreeChunk(sfx_explosion);
                                                                                                              if (sfx_winner)      Mix_FreeChunk(sfx_winner);
                                                                                                              Mix_CloseAudio();
                                                                                                              Mix_Quit();

                                                                                                              // Destroy textures and free surfaces
                                                                                                              if (t_win)  SDL_DestroyTexture(t_win);
                                                                                                              if (s_win)  SDL_FreeSurface(s_win);
                                                                                                              if (t_exp)  SDL_DestroyTexture(t_exp);
                                                                                                              if (s_exp)  SDL_FreeSurface(s_exp);
                                                                                                              for (int i = 0; i < CANNON_FRAMES; i++) {
                                                                                                                  if (t_c[i]) SDL_DestroyTexture(t_c[i]);
                                                                                                                  if (s_c[i]) SDL_FreeSurface(s_c[i]);
                                                                                                              }
                                                                                                              if (t_bu1) SDL_DestroyTexture(t_bu1);
                                                                                                              if (t_bu2) SDL_DestroyTexture(t_bu2);
                                                                                                              if (s_bu1) SDL_FreeSurface(s_bu1);
                                                                                                              if (s_bu2) SDL_FreeSurface(s_bu2);

                                                                                                              // Destroy renderer and window
                                                                                                              if (ren) SDL_DestroyRenderer(ren);
                                                                                                              if (win) SDL_DestroyWindow(win);

                                                                                                              IMG_Quit();
                                                                                                              SDL_Quit();
                                                                                                              return 0;
                                                                                                          }
