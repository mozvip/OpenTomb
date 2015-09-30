
#include <SDL2/SDL.h>
#include <SDL2/SDL_platform.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_opengl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include "al/AL/al.h"
#include "al/AL/alc.h"
}

#include "core/system.h"
#include "core/gl_util.h"
#include "core/gl_font.h"
#include "core/console.h"
#include "core/redblack.h"
#include "core/vmath.h"
#include "render/camera.h"
#include "render/render.h"
#include "vt/vt_level.h"
#include "game.h"
#include "audio.h"
#include "mesh.h"
#include "skeletal_model.h"
#include "gui.h"
#include "entity.h"
#include "gameflow.h"
#include "room.h"
#include "world.h"
#include "resource.h"
#include "script.h"
#include "engine.h"
#include "physics.h"
#include "controls.h"


static SDL_Window             *sdl_window     = NULL;
static SDL_Joystick           *sdl_joystick   = NULL;
static SDL_GameController     *sdl_controller = NULL;
static SDL_Haptic             *sdl_haptic     = NULL;
static SDL_GLContext           sdl_gl_context = 0;
static ALCdevice              *al_device      = NULL;
static ALCcontext             *al_context     = NULL;

static int                      engine_done   = 0;

float time_scale = 1.0f;

engine_container_p      last_cont = NULL;

struct engine_control_state_s           control_states = {0};
struct control_settings_s               control_mapper = {0};
struct audio_settings_s                 audio_settings = {0};
float                                   engine_frame_time = 0.0;

lua_State                              *engine_lua = NULL;
struct camera_s                         engine_camera;
struct world_s                          engine_world;


engine_container_p Container_Create()
{
    engine_container_p ret;

    ret = (engine_container_p)malloc(sizeof(engine_container_t));
    ret->next = NULL;
    ret->object = NULL;
    ret->object_type = 0;
    return ret;
}


void Engine_Init_Pre();
void Engine_Init_Post();
void Engine_InitGL();
void Engine_InitAL();
void Engine_InitSDLImage();
void Engine_InitSDLVideo();
void Engine_InitSDLControls();
void Engine_InitDefaultGlobals();

void Engine_Display();
void Engine_PollSDLEvents();
void Engine_Resize(int nominalW, int nominalH, int pixelsW, int pixelsH);

void ShowDebugInfo();

void Engine_Start(const char *config_name)
{
#if defined(__MACOSX__)
    FindConfigFile();
#endif

    Engine_InitDefaultGlobals();
    Engine_LoadConfig(config_name);

    // Primary initialization.
    Engine_Init_Pre();

    // Init generic SDL interfaces.
    Engine_InitSDLControls();
    Engine_InitSDLVideo();
    Engine_InitAL();

#if !defined(__MACOSX__)
    Engine_InitSDLImage();
#endif

    // Additional OpenGL initialization.
    Engine_InitGL();
    renderer.DoShaders();

    // Secondary (deferred) initialization.
    Engine_Init_Post();

    // Make splash screen.
    Gui_LoadScreenAssignPic("resource/graphics/legal.png");

    // Initial window resize.
    Engine_Resize(screen_info.w, screen_info.h, screen_info.w, screen_info.h);

    // Clearing up memory for initial level loading.
    World_Prepare(&engine_world);

    // Setting up mouse.
    SDL_SetRelativeMouseMode(SDL_TRUE);
    SDL_WarpMouseInWindow(sdl_window, screen_info.w/2, screen_info.h/2);
    SDL_ShowCursor(0);

    luaL_dofile(engine_lua, "autoexec.lua");
}


void Engine_Shutdown(int val)
{
    renderer.SetWorld(NULL);
    lua_Clean(engine_lua);
    World_Clear(&engine_world);

    if(engine_lua)
    {
        lua_close(engine_lua);
        engine_lua = NULL;
    }

    Con_Destroy();
    Sys_Destroy();
    Physics_Destroy();
    Gui_Destroy();

    /* no more renderings */
    SDL_GL_DeleteContext(sdl_gl_context);
    sdl_gl_context = 0;
    SDL_DestroyWindow(sdl_window);
    sdl_window = NULL;

    if(sdl_joystick)
    {
        SDL_JoystickClose(sdl_joystick);
        sdl_joystick = NULL;
    }

    if(sdl_controller)
    {
        SDL_GameControllerClose(sdl_controller);
        sdl_controller = NULL;
    }

    if(sdl_haptic)
    {
        SDL_HapticClose(sdl_haptic);
        sdl_haptic = NULL;
    }

    if(al_context)  // T4Larson <t4larson@gmail.com>: fixed
    {
        alcMakeContextCurrent(NULL);
        alcDestroyContext(al_context);
        al_context = NULL;
    }

    if(al_device)
    {
        alcCloseDevice(al_device);
        al_device = NULL;
    }

    Sys_Destroy();
    IMG_Quit();
    SDL_Quit();

    exit(val);
}


void Engine_SetDone()
{
    engine_done = 1;
}


void Engine_InitDefaultGlobals()
{
    Sys_InitGlobals();
    Con_InitGlobals();
    Controls_InitGlobals();
    Game_InitGlobals();
    Audio_InitGlobals();
}

// First stage of initialization.
void Engine_Init_Pre()
{
    /* Console must be initialized previously! some functions uses CON_AddLine before GL initialization!
     * Rendering activation may be done later. */

    Sys_Init();
    Con_Init();
    Con_SetExecFunction(Engine_ExecCmd);
    Script_LuaInit();

    Script_CallVoidFunc(engine_lua, "loadscript_pre", true);

    Gameflow_Init();
    Cam_Init(&engine_camera);

    Physics_Init();
}

// Second stage of initialization.
void Engine_Init_Post()
{
    Script_CallVoidFunc(engine_lua, "loadscript_post", true);

    Con_InitFont();

    Gui_Init();

    Con_AddLine("Engine inited!", FONTSTYLE_CONSOLE_EVENT);
}


void Engine_InitGL()
{
    InitGLExtFuncs();
    qglClearColor(0.0, 0.0, 0.0, 1.0);

    qglEnable(GL_DEPTH_TEST);
    qglDepthFunc(GL_LEQUAL);

    if(renderer.settings.antialias)
    {
        qglEnable(GL_MULTISAMPLE);
    }
    else
    {
       qglDisable(GL_MULTISAMPLE);
    }

    // Default state: Vertex array and color array are enabled, all others disabled.. Drawable
    // items can rely on Vertex array to be enabled (but pointer can be
    // anything). They have to enable other arrays based on their need and then
    // return to default state
    qglEnableClientState(GL_VERTEX_ARRAY);
    qglEnableClientState(GL_COLOR_ARRAY);

    // function use anyway.
    qglAlphaFunc(GL_GEQUAL, 0.5);
}


void Engine_InitAL()
{
    ALCint paramList[] = {
        ALC_STEREO_SOURCES,  TR_AUDIO_STREAM_NUMSOURCES,
        ALC_MONO_SOURCES,   (TR_AUDIO_MAX_CHANNELS - TR_AUDIO_STREAM_NUMSOURCES),
        ALC_FREQUENCY,       44100, 0};

    Con_Printf("Audio driver: %s", SDL_GetCurrentAudioDriver());

    al_device = alcOpenDevice(NULL);
    if (!al_device)
    {
        Sys_DebugLog(SYS_LOG_FILENAME, "InitAL: No AL audio devices!");
        return;
    }

    al_context = alcCreateContext(al_device, paramList);
    if(!alcMakeContextCurrent(al_context))
    {
        Sys_DebugLog(SYS_LOG_FILENAME, "InitAL: AL context is not current!");
        return;
    }

    alSpeedOfSound(330.0 * 512.0);
    alDopplerVelocity(330.0 * 510.0);
    alDistanceModel(AL_LINEAR_DISTANCE_CLAMPED);
}


#if !defined(__MACOSX__)
void Engine_InitSDLImage()
{
    int flags = IMG_INIT_JPG | IMG_INIT_PNG;
    int init  = IMG_Init(flags);

    if((init & flags) != flags)
    {
        Sys_DebugLog(SYS_LOG_FILENAME, "SDL_Image error: failed to initialize JPG and/or PNG support.");
    }
}
#endif


void Engine_InitSDLVideo()
{
    Uint32 video_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_MOUSE_FOCUS | SDL_WINDOW_INPUT_FOCUS;
    PFNGLGETSTRINGPROC lglGetString = NULL;

    if(screen_info.FS_flag)
    {
        video_flags |= SDL_WINDOW_FULLSCREEN;
    }
    else
    {
        video_flags |= (SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);
    }

    ///@TODO: is it really needede for correct work?
    if(SDL_GL_LoadLibrary(NULL) < 0)
    {
        Sys_Error("Could not init OpenGL driver");
    }

    // Check for correct number of antialias samples.
    if(renderer.settings.antialias)
    {
        GLint maxSamples = 0;
        PFNGLGETIINTEGERVPROC lglGetIntegerv = NULL;
        /* I do not know why, but settings of this temporary window (zero position / size) are applied to the main window, ignoring screen settings */
        sdl_window     = SDL_CreateWindow(NULL, screen_info.x, screen_info.y, screen_info.w, screen_info.h, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        sdl_gl_context = SDL_GL_CreateContext(sdl_window);
        SDL_GL_MakeCurrent(sdl_window, sdl_gl_context);

        lglGetIntegerv = (PFNGLGETIINTEGERVPROC)SDL_GL_GetProcAddress("glGetIntegerv");
        lglGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
        maxSamples = (maxSamples > 16)?(16):(maxSamples);                       // Fix for faulty GL max. sample number.

        if(renderer.settings.antialias_samples > maxSamples)
        {
            renderer.settings.antialias_samples = maxSamples;                   // Limit to max.
            if(maxSamples == 0)
            {
                renderer.settings.antialias = 0;
                Sys_DebugLog(SYS_LOG_FILENAME, "InitSDLVideo: can't use antialiasing");
            }
            else
            {
                Sys_DebugLog(SYS_LOG_FILENAME, "InitSDLVideo: wrong AA sample number, using %d", maxSamples);
            }
        }

        SDL_GL_DeleteContext(sdl_gl_context);
        SDL_DestroyWindow(sdl_window);

        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, renderer.settings.antialias);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, renderer.settings.antialias_samples);
    }
    else
    {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
    }

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, renderer.settings.z_depth);
#if STENCIL_FRUSTUM
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
#endif
    // set the opengl context version
    //SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    //SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    //SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    //SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);

    sdl_window = SDL_CreateWindow("OpenTomb", screen_info.x, screen_info.y, screen_info.w, screen_info.h, video_flags);
    sdl_gl_context = SDL_GL_CreateContext(sdl_window);
    SDL_GL_MakeCurrent(sdl_window, sdl_gl_context);

    lglGetString = (PFNGLGETSTRINGPROC)SDL_GL_GetProcAddress("glGetString");
    Con_AddLine((const char*)lglGetString(GL_VENDOR), FONTSTYLE_CONSOLE_INFO);
    Con_AddLine((const char*)lglGetString(GL_RENDERER), FONTSTYLE_CONSOLE_INFO);
    Con_Printf("OpenGL version %s", lglGetString(GL_VERSION));
    Con_AddLine((const char*)lglGetString(GL_SHADING_LANGUAGE_VERSION), FONTSTYLE_CONSOLE_INFO);
}


void Engine_InitSDLControls()
{
    int    NumJoysticks;
    Uint32 init_flags    = SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS;   // These flags are used in any case.

    if(control_mapper.use_joy == 1)
    {
        init_flags |= SDL_INIT_GAMECONTROLLER;                                  // Update init flags for joystick.

        if(control_mapper.joy_rumble)
        {
            init_flags |= SDL_INIT_HAPTIC;                                      // Update init flags for force feedback.
        }

        SDL_Init(init_flags);

        NumJoysticks = SDL_NumJoysticks();
        if((NumJoysticks < 1) || ((NumJoysticks - 1) < control_mapper.joy_number))
        {
            Sys_DebugLog(SYS_LOG_FILENAME, "Error: there is no joystick #%d present.", control_mapper.joy_number);
            return;
        }

        if(SDL_IsGameController(control_mapper.joy_number))                     // If joystick has mapping (e.g. X360 controller)
        {
            SDL_GameControllerEventState(SDL_ENABLE);                           // Use GameController API
            sdl_controller = SDL_GameControllerOpen(control_mapper.joy_number);

            if(!sdl_controller)
            {
                Sys_DebugLog(SYS_LOG_FILENAME, "Error: can't open game controller #%d.", control_mapper.joy_number);
                SDL_GameControllerEventState(SDL_DISABLE);                      // If controller init failed, close state.
                control_mapper.use_joy = 0;
            }
            else if(control_mapper.joy_rumble)                                  // Create force feedback interface.
            {
                sdl_haptic = SDL_HapticOpenFromJoystick(SDL_GameControllerGetJoystick(sdl_controller));
                if(!sdl_haptic)
                {
                    Sys_DebugLog(SYS_LOG_FILENAME, "Error: can't initialize haptic from game controller #%d.", control_mapper.joy_number);
                }
            }
        }
        else
        {
            SDL_JoystickEventState(SDL_ENABLE);                                 // If joystick isn't mapped, use generic API.
            sdl_joystick = SDL_JoystickOpen(control_mapper.joy_number);

            if(!sdl_joystick)
            {
                Sys_DebugLog(SYS_LOG_FILENAME, "Error: can't open joystick #%d.", control_mapper.joy_number);
                SDL_JoystickEventState(SDL_DISABLE);                            // If joystick init failed, close state.
                control_mapper.use_joy = 0;
            }
            else if(control_mapper.joy_rumble)                                  // Create force feedback interface.
            {
                sdl_haptic = SDL_HapticOpenFromJoystick(sdl_joystick);
                if(!sdl_haptic)
                {
                    Sys_DebugLog(SYS_LOG_FILENAME, "Error: can't initialize haptic from joystick #%d.", control_mapper.joy_number);
                }
            }
        }

        if(sdl_haptic)                                                          // To check if force feedback is working or not.
        {
            SDL_HapticRumbleInit(sdl_haptic);
            SDL_HapticRumblePlay(sdl_haptic, 1.0, 300);
        }
    }
    else
    {
        SDL_Init(init_flags);
    }
}


void Engine_LoadConfig(const char *filename)
{
    if((filename != NULL) && Sys_FileFound(filename, 0))
    {
        lua_State *lua = luaL_newstate();
        if(lua != NULL)
        {
            luaL_openlibs(lua);
            lua_register(lua, "bind", lua_BindKey);                             // get and set key bindings
            luaL_dofile(lua, filename);

            lua_ParseScreen(lua, &screen_info);
            lua_ParseRender(lua, &renderer.settings);
            lua_ParseAudio(lua, &audio_settings);
            lua_ParseConsole(lua);
            lua_ParseControls(lua, &control_mapper);
            lua_close(lua);
        }
    }
    else
    {
        Sys_Warn("Could not find \"%s\"", filename);
    }
}


void Engine_SaveConfig(const char *filename)
{

}


void Engine_Display()
{
    if(!engine_done)
    {
        qglClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);//| GL_ACCUM_BUFFER_BIT);

        Cam_Apply(&engine_camera);
        Cam_RecalcClipPlanes(&engine_camera);
        // GL_VERTEX_ARRAY | GL_COLOR_ARRAY
        if(screen_info.show_debuginfo)
        {
            ShowDebugInfo();
        }

        qglPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT); ///@PUSH <- GL_VERTEX_ARRAY | GL_COLOR_ARRAY
        qglEnableClientState(GL_NORMAL_ARRAY);
        qglEnableClientState(GL_TEXTURE_COORD_ARRAY);

        qglFrontFace(GL_CW);

        renderer.GenWorldList(&engine_camera);
        renderer.DrawList();

        Gui_SwitchGLMode(1);
        qglEnable(GL_ALPHA_TEST);

        Gui_DrawNotifier();
        qglPopClientAttrib();        ///@POP -> GL_VERTEX_ARRAY | GL_COLOR_ARRAY
        Gui_Render();
        Gui_SwitchGLMode(0);

        renderer.DrawListDebugLines();

        SDL_GL_SwapWindow(sdl_window);
    }
}


void Engine_GLSwapWindow()
{
    SDL_GL_SwapWindow(sdl_window);
}


void Engine_Resize(int nominalW, int nominalH, int pixelsW, int pixelsH)
{
    screen_info.w = nominalW;
    screen_info.h = nominalH;

    screen_info.w_unit = (float)nominalW / GUI_SCREEN_METERING_RESOLUTION;
    screen_info.h_unit = (float)nominalH / GUI_SCREEN_METERING_RESOLUTION;
    screen_info.scale_factor = (screen_info.w < screen_info.h)?(screen_info.h_unit):(screen_info.w_unit);

    Gui_Resize();

    Cam_SetFovAspect(&engine_camera, screen_info.fov, (float)nominalW/(float)nominalH);
    Cam_RecalcClipPlanes(&engine_camera);

    qglViewport(0, 0, pixelsW, pixelsH);
}


void Engine_PollSDLEvents()
{
    SDL_Event event;
    static int mouse_setup = 0;
    //const float color[3] = {1.0f, 0.0f, 0.0f};
    float from[3], to[3];

    while(SDL_PollEvent(&event))
    {
        switch(event.type)
        {
            case SDL_MOUSEMOTION:
                if(!Con_IsShown() && control_states.mouse_look != 0 &&
                    ((event.motion.x != (screen_info.w / 2)) ||
                     (event.motion.y != (screen_info.h / 2))))
                {
                    if(mouse_setup)                                             // it is not perfect way, but cursor
                    {                                                           // every engine start is in one place
                        control_states.look_axis_x = event.motion.xrel * control_mapper.mouse_sensitivity * 0.01;
                        control_states.look_axis_y = event.motion.yrel * control_mapper.mouse_sensitivity * 0.01;
                    }

                    if((event.motion.x < ((screen_info.w / 2) - (screen_info.w / 4))) ||
                       (event.motion.x > ((screen_info.w / 2) + (screen_info.w / 4))) ||
                       (event.motion.y < ((screen_info.h / 2)-(screen_info.h / 4))) ||
                       (event.motion.y > ((screen_info.h / 2)+(screen_info.h / 4))))
                    {
                        SDL_WarpMouseInWindow(sdl_window, screen_info.w/2, screen_info.h/2);
                    }
                }
                mouse_setup = 1;
                break;

            case SDL_MOUSEBUTTONDOWN:
                if(event.button.button == 1) //LM = 1, MM = 2, RM = 3
                {
                    Controls_PrimaryMouseDown(from, to);
                }
                else if(event.button.button == 3)
                {
                    Controls_SecondaryMouseDown(&last_cont);
                }
                break;

            // Controller events are only invoked when joystick is initialized as
            // game controller, otherwise, generic joystick event will be used.
            case SDL_CONTROLLERAXISMOTION:
                Controls_WrapGameControllerAxis(event.caxis.axis, event.caxis.value);
                break;

            case SDL_CONTROLLERBUTTONDOWN:
            case SDL_CONTROLLERBUTTONUP:
                Controls_WrapGameControllerKey(event.cbutton.button, event.cbutton.state);
                break;

            // Joystick events are still invoked, even if joystick is initialized as game
            // controller - that's why we need sdl_joystick checking - to filter out
            // duplicate event calls.

            case SDL_JOYAXISMOTION:
                if(sdl_joystick)
                {
                    Controls_JoyAxis(event.jaxis.axis, event.jaxis.value);
                }
                break;

            case SDL_JOYHATMOTION:
                if(sdl_joystick)
                {
                    Controls_JoyHat(event.jhat.value);
                }
                break;

            case SDL_JOYBUTTONDOWN:
            case SDL_JOYBUTTONUP:
                // NOTE: Joystick button numbers are passed with added JOY_BUTTON_MASK (1000).
                if(sdl_joystick)
                {
                    Controls_Key((event.jbutton.button + JOY_BUTTON_MASK), event.jbutton.state);
                }
                break;

            case SDL_TEXTINPUT:
            case SDL_TEXTEDITING:
                if(Con_IsShown() && event.key.state)
                {
                    Con_Filter(event.text.text);
                    return;
                }
                break;

            case SDL_KEYUP:
            case SDL_KEYDOWN:
                if( (event.key.keysym.sym == SDLK_F4) &&
                    (event.key.state == SDL_PRESSED)  &&
                    (event.key.keysym.mod & KMOD_ALT) )
                {
                    Engine_SetDone();
                    break;
                }

                if(Con_IsShown() && event.key.state)
                {
                    switch (event.key.keysym.sym)
                    {
                        case SDLK_RETURN:
                        case SDLK_UP:
                        case SDLK_DOWN:
                        case SDLK_LEFT:
                        case SDLK_RIGHT:
                        case SDLK_HOME:
                        case SDLK_END:
                        case SDLK_BACKSPACE:
                        case SDLK_DELETE:
                            Con_Edit(event.key.keysym.sym);
                            break;
                        default:
                            break;
                    }
                    return;
                }
                else
                {
                    Controls_Key(event.key.keysym.sym, event.key.state);
                    // DEBUG KEYBOARD COMMANDS
                    Controls_DebugKeys(event.key.keysym.sym, event.key.state);
                }
                break;

            case SDL_QUIT:
                Engine_SetDone();
                break;

            case SDL_WINDOWEVENT:
                if(event.window.event == SDL_WINDOWEVENT_RESIZED)
                {
                    Engine_Resize(event.window.data1, event.window.data2, event.window.data1, event.window.data2);
                }
                break;

            default:
                break;
        }
    }
    //renderer.debugDrawer->DrawLine(from, to, color, color);
}


void Engine_JoyRumble(float power, int time)
{
    // JoyRumble is a simple wrapper for SDL's haptic rumble play.
    if(sdl_haptic)
    {
        SDL_HapticRumblePlay(sdl_haptic, power, time);
    }
}


void Engine_MainLoop()
{
    float time = 0.0f;
    float newtime = 0.0f;
    float oldtime = 0.0f;
    float time_cycl = 0.0f;

    const int max_cycles = 64;
    int cycles = 0;
    char fps_str[32] = "0.0";

    while(!engine_done)
    {
        newtime = Sys_FloatTime();
        time = newtime - oldtime;
        oldtime = newtime;
        time *= time_scale;

        engine_frame_time = time;

        if(cycles < max_cycles)
        {
            cycles++;
            time_cycl += time;
        }
        else
        {
            screen_info.fps = ((float)max_cycles / time_cycl);
            snprintf(fps_str, 32, "%.1f", screen_info.fps);
            cycles = 0;
            time_cycl = 0.0f;
        }

        gui_text_line_p fps = Gui_OutTextXY(10.0f, 10.0f, fps_str);
        fps->Xanchor    = GUI_ANCHOR_HOR_RIGHT;
        fps->Yanchor    = GUI_ANCHOR_VERT_BOTTOM;
        fps->font_id    = FONT_PRIMARY;
        fps->style_id   = FONTSTYLE_MENU_TITLE;
        fps->show       = 1;

        Sys_ResetTempMem();
        Engine_PollSDLEvents();
        Game_Frame(time);
        Gameflow_Do();

        Engine_Display();
    }
}


void ShowDebugInfo()
{
    entity_p ent;
    ent = engine_world.Character;
    if(ent && ent->character)
    {
        /*height_info_p fc = &ent->character->height_info
        txt = Gui_OutTextXY(20.0 / screen_info.w, 80.0 / screen_info.w, "Z_min = %d, Z_max = %d, W = %d", (int)fc->floor_point.m_floats[2], (int)fc->ceiling_point.m_floats[2], (int)fc->water_level);
        */

        Gui_OutTextXY(30.0, 30.0, "last_anim = %03d, curr_anim = %03d, next_anim = %03d, last_st = %03d, next_st = %03d", ent->bf->animations.last_animation, ent->bf->animations.current_animation, ent->bf->animations.next_animation, ent->bf->animations.last_state, ent->bf->animations.next_state);
        //Gui_OutTextXY(30.0, 30.0, "curr_anim = %03d, next_anim = %03d, curr_frame = %03d, next_frame = %03d", ent->bf->animations.current_animation, ent->bf->animations.next_animation, ent->bf->animations.current_frame, ent->bf->animations.next_frame);
        //Gui_OutTextXY(NULL, 20, 8, "posX = %f, posY = %f, posZ = %f", engine_world.Character->transform[12], engine_world.Character->transform[13], engine_world.Character->transform[14]);
    }

    if(last_cont != NULL)
    {
        switch(last_cont->object_type)
        {
            case OBJECT_ENTITY:
                Gui_OutTextXY(30.0, 60.0, "cont_entity: id = %d, model = %d", ((entity_p)last_cont->object)->id, ((entity_p)last_cont->object)->bf->animations.model->id);
                break;

            case OBJECT_STATIC_MESH:
                Gui_OutTextXY(30.0, 60.0, "cont_static: id = %d", ((static_mesh_p)last_cont->object)->object_id);
                break;

            case OBJECT_ROOM_BASE:
                Gui_OutTextXY(30.0, 60.0, "cont_room: id = %d", ((room_p)last_cont->object)->id);
                break;
        }

    }

    if(engine_camera.current_room != NULL)
    {
        room_sector_p rs = Room_GetSectorRaw(engine_camera.current_room, engine_camera.pos);
        if(rs != NULL)
        {
            Gui_OutTextXY(30.0, 90.0, "room = (id = %d, sx = %d, sy = %d)", engine_camera.current_room->id, rs->index_x, rs->index_y);
            Gui_OutTextXY(30.0, 120.0, "room_below = %d, room_above = %d", (rs->sector_below != NULL)?(rs->sector_below->owner_room->id):(-1), (rs->sector_above != NULL)?(rs->sector_above->owner_room->id):(-1));
        }
    }
    Gui_OutTextXY(30.0, 150.0, "cam_pos = (%.1f, %.1f, %.1f)", engine_camera.pos[0], engine_camera.pos[1], engine_camera.pos[2]);
}


/*
 * MISC ENGINE FUNCTIONALITY
 */
int Engine_GetLevelFormat(const char *name)
{
    // PLACEHOLDER: Currently, only PC levels are supported.

    return LEVEL_FORMAT_PC;
}


int Engine_GetPCLevelVersion(const char *name)
{
    int ret = TR_UNKNOWN;
    int len = strlen(name);
    FILE *ff;

    if(len < 5)
    {
        return ret;                                                             // Wrong (too short) filename
    }

    ff = fopen(name, "rb");
    if(ff)
    {
        char ext[5];
        uint8_t check[4];

        ext[0] = name[len-4];                                                   // .
        ext[1] = toupper(name[len-3]);                                          // P
        ext[2] = toupper(name[len-2]);                                          // H
        ext[3] = toupper(name[len-1]);                                          // D
        ext[4] = 0;
        fread(check, 4, 1, ff);

        if(!strncmp(ext, ".PHD", 4))                                            //
        {
            if(check[0] == 0x20 &&
               check[1] == 0x00 &&
               check[2] == 0x00 &&
               check[3] == 0x00)
            {
                ret = TR_I;                                                     // TR_I ? OR TR_I_DEMO
            }
            else
            {
                ret = TR_UNKNOWN;
            }
        }
        else if(!strncmp(ext, ".TUB", 4))
        {
            if(check[0] == 0x20 &&
               check[1] == 0x00 &&
               check[2] == 0x00 &&
               check[3] == 0x00)
            {
                ret = TR_I_UB;                                                  // TR_I_UB
            }
            else
            {
                ret = TR_UNKNOWN;
            }
        }
        else if(!strncmp(ext, ".TR2", 4))
        {
            if(check[0] == 0x2D &&
               check[1] == 0x00 &&
               check[2] == 0x00 &&
               check[3] == 0x00)
            {
                ret = TR_II;                                                    // TR_II
            }
            else if((check[0] == 0x38 || check[0] == 0x34) &&
                    (check[1] == 0x00) &&
                    (check[2] == 0x18 || check[2] == 0x08) &&
                    (check[3] == 0xFF))
            {
                ret = TR_III;                                                   // TR_III
            }
            else
            {
                ret = TR_UNKNOWN;
            }
        }
        else if(!strncmp(ext, ".TR4", 4))
        {
            if(check[0] == 0x54 &&                                              // T
               check[1] == 0x52 &&                                              // R
               check[2] == 0x34 &&                                              // 4
               check[3] == 0x00)
            {
                ret = TR_IV;                                                    // OR TR TR_IV_DEMO
            }
            else if(check[0] == 0x54 &&                                         // T
                    check[1] == 0x52 &&                                         // R
                    check[2] == 0x34 &&                                         // 4
                    check[3] == 0x63)                                           //
            {
                ret = TR_IV;                                                    // TRLE
            }
            else if(check[0] == 0xF0 &&                                         // T
                    check[1] == 0xFF &&                                         // R
                    check[2] == 0xFF &&                                         // 4
                    check[3] == 0xFF)
            {
                ret = TR_IV;                                                    // BOGUS (OpenRaider =))
            }
            else
            {
                ret = TR_UNKNOWN;
            }
        }
        else if(!strncmp(ext, ".TRC", 4))
        {
            if(check[0] == 0x54 &&                                              // T
               check[1] == 0x52 &&                                              // R
               check[2] == 0x34 &&                                              // 4
               check[3] == 0x00)
            {
                ret = TR_V;                                                     // TR_V
            }
            else
            {
                ret = TR_UNKNOWN;
            }
        }
        else                                                                    // unknown ext.
        {
            ret = TR_UNKNOWN;
        }

        fclose(ff);
    }

    return ret;
}


void Engine_GetLevelName(char *name, const char *path)
{
    int i, len, start, ext;

    if(!path || (path[0] == 0x00))
    {
        name[0] = 0x00;
        return;
    }

    ext = len = strlen(path);
    start = 0;

    for(i=len;i>=0;i--)
    {
        if(path[i] == '.')
        {
            ext = i;
        }
        if(path[i] == '\\' || path[i] == '/')
        {
            start = i + 1;
            break;
        }
    }

    for(i=start;i<ext && i-start<LEVEL_NAME_MAX_LEN-1;i++)
    {
        name[i-start] = path[i];
    }
    name[i-start] = 0;
}


void Engine_GetLevelScriptName(int game_version, char *name, const char *postfix)
{
    char level_name[LEVEL_NAME_MAX_LEN];
    Engine_GetLevelName(level_name, gameflow_manager.CurrentLevelPath);

    name[0] = 0;

    strcat(name, "scripts/level/");

    if(game_version < TR_II)
    {
        strcat(name, "tr1/");
    }
    else if(game_version < TR_III)
    {
        strcat(name, "tr2/");
    }
    else if(game_version < TR_IV)
    {
        strcat(name, "tr3/");
    }
    else if(game_version < TR_V)
    {
        strcat(name, "tr4/");
    }
    else
    {
        strcat(name, "tr5/");
    }

    for(char *ch=level_name;*ch!=0;ch++)
    {
        *ch = toupper(*ch);
    }

    strcat(name, level_name);
    if(postfix)
    {
        strcat(name, postfix);
    }
    strcat(name, ".lua");
}


bool Engine_LoadPCLevel(const char *name)
{
    VT_Level *tr_level = new VT_Level();

    int trv = Engine_GetPCLevelVersion(name);
    if(trv == TR_UNKNOWN) return false;

    tr_level->read_level(name, trv);
    tr_level->prepare_level();
    //tr_level->dump_textures();

    World_Open(&engine_world, tr_level);

    char buf[LEVEL_NAME_MAX_LEN] = {0x00};
    Engine_GetLevelName(buf, name);

    Con_Notify("loaded PC level");
    Con_Notify("version = %d, map = \"%s\"", trv, buf);
    Con_Notify("rooms count = %d", engine_world.room_count);

    delete tr_level;

    return true;
}


int Engine_LoadMap(const char *name)
{
    if(!Sys_FileFound(name, 0))
    {
        Con_Warning("file not found: \"%s\"", name);
        return 0;
    }

    engine_camera.current_room = NULL;
    renderer.SetWorld(NULL);
    Gui_DrawLoadScreen(0);

    strncpy(gameflow_manager.CurrentLevelPath, name, MAX_ENGINE_PATH);          // it is needed for "not in the game" levels or correct saves loading.

    Gui_DrawLoadScreen(50);

    lua_Clean(engine_lua);
    Gui_DrawLoadScreen(100);


    // Here we can place different platform-specific level loading routines.
    switch(Engine_GetLevelFormat(name))
    {
        case LEVEL_FORMAT_PC:
            if(!Engine_LoadPCLevel(name))
            {
                return 0;
            }
            break;

        /*case LEVEL_FORMAT_PSX:
            return 0;
            break;

        case LEVEL_FORMAT_DC:
            return 0;
            break;

        case LEVEL_FORMAT_OPENTOMB:
            return 0;
            break;*/

        default:
            return 0;
    }

    Audio_Init();

    engine_world.id   = 0;
    engine_world.name = 0;
    engine_world.type = 0;

    Game_Prepare();

    renderer.SetWorld(&engine_world);

    Gui_DrawLoadScreen(1000);
    Gui_NotifierStop();

    return 1;
}


int Engine_ExecCmd(char *ch)
{
    char token[1024];

    while(ch != NULL)
    {
        char *pch = ch;
        ch = parse_token(ch, token);
        if(!strcmp(token, "help"))
        {
            Con_AddLine("Available commands:\0", FONTSTYLE_CONSOLE_WARNING);
            Con_AddLine("help - show help info\0", FONTSTYLE_CONSOLE_NOTIFY);
            Con_AddLine("loadMap(\"file_name\") - load level \"file_name\"\0", FONTSTYLE_CONSOLE_NOTIFY);
            Con_AddLine("save, load - save and load game state in \"file_name\"\0", FONTSTYLE_CONSOLE_NOTIFY);
            Con_AddLine("exit - close program\0", FONTSTYLE_CONSOLE_NOTIFY);
            Con_AddLine("cls - clean console\0", FONTSTYLE_CONSOLE_NOTIFY);
            Con_AddLine("show_fps - switch show fps flag\0", FONTSTYLE_CONSOLE_NOTIFY);
            Con_AddLine("spacing - read and write spacing\0", FONTSTYLE_CONSOLE_NOTIFY);
            Con_AddLine("showing_lines - read and write number of showing lines\0", FONTSTYLE_CONSOLE_NOTIFY);
            Con_AddLine("cvars - lua's table of cvar's, to see them type: show_table(cvars)\0", FONTSTYLE_CONSOLE_NOTIFY);
            Con_AddLine("free_look - switch camera mode\0", FONTSTYLE_CONSOLE_NOTIFY);
            Con_AddLine("cam_distance - camera distance to actor\0", FONTSTYLE_CONSOLE_NOTIFY);
            Con_AddLine("r_wireframe, r_portals, r_frustums, r_room_boxes, r_boxes, r_normals, r_skip_room - render modes\0", FONTSTYLE_CONSOLE_NOTIFY);
            Con_AddLine("playsound(id) - play specified sound\0", FONTSTYLE_CONSOLE_NOTIFY);
            Con_AddLine("stopsound(id) - stop specified sound\0", FONTSTYLE_CONSOLE_NOTIFY);
            Con_AddLine("Watch out for case sensitive commands!\0", FONTSTYLE_CONSOLE_WARNING);
        }
        else if(!strcmp(token, "goto"))
        {
            control_states.free_look = 1;
            engine_camera.pos[0] = SC_ParseFloat(&ch);
            engine_camera.pos[1] = SC_ParseFloat(&ch);
            engine_camera.pos[2] = SC_ParseFloat(&ch);
            return 1;
        }
        else if(!strcmp(token, "save"))
        {
            ch = parse_token(ch, token);
            if(NULL != ch)
            {
                Game_Save(token);
            }
            return 1;
        }
        else if(!strcmp(token, "load"))
        {
            ch = parse_token(ch, token);
            if(NULL != ch)
            {
                Game_Load(token);
            }
            return 1;
        }
        else if(!strcmp(token, "exit"))
        {
            Engine_Shutdown(0);
            return 1;
        }
        else if(!strcmp(token, "cls"))
        {
            Con_Clean();
            return 1;
        }
        else if(!strcmp(token, "spacing"))
        {
            ch = parse_token(ch, token);
            if(NULL == ch)
            {
                Con_Notify("spacing = %d", Con_GetLineInterval());
                return 1;
            }
            Con_SetLineInterval(atof(token));
            return 1;
        }
        else if(!strcmp(token, "showing_lines"))
        {
            ch = parse_token(ch, token);
            if(NULL == ch)
            {
                Con_Notify("showing lines = %d", Con_GetShowingLines());
                return 1;
            }
            else
            {
                Con_SetShowingLines(atoi(token));
            }
            return 1;
        }
        else if(!strcmp(token, "r_wireframe"))
        {
            renderer.r_flags ^= R_DRAW_WIRE;
            return 1;
        }
        else if(!strcmp(token, "r_points"))
        {
            renderer.r_flags ^= R_DRAW_POINTS;
            return 1;
        }
        else if(!strcmp(token, "r_coll"))
        {
            renderer.r_flags ^= R_DRAW_COLL;
            return 1;
        }
        else if(!strcmp(token, "r_normals"))
        {
            renderer.r_flags ^= R_DRAW_NORMALS;
            return 1;
        }
        else if(!strcmp(token, "r_portals"))
        {
            renderer.r_flags ^= R_DRAW_PORTALS;
            return 1;
        }
        else if(!strcmp(token, "r_frustums"))
        {
            renderer.r_flags ^= R_DRAW_FRUSTUMS;
            return 1;
        }
        else if(!strcmp(token, "r_room_boxes"))
        {
            renderer.r_flags ^= R_DRAW_ROOMBOXES;
            return 1;
        }
        else if(!strcmp(token, "r_boxes"))
        {
            renderer.r_flags ^= R_DRAW_BOXES;
            return 1;
        }
        else if(!strcmp(token, "r_axis"))
        {
            renderer.r_flags ^= R_DRAW_AXIS;
            return 1;
        }
        else if(!strcmp(token, "r_nullmeshes"))
        {
            renderer.r_flags ^= R_DRAW_NULLMESHES;
            return 1;
        }
        else if(!strcmp(token, "r_dummy_statics"))
        {
            renderer.r_flags ^= R_DRAW_DUMMY_STATICS;
            return 1;
        }
        else if(!strcmp(token, "r_skip_room"))
        {
            renderer.r_flags ^= R_SKIP_ROOM;
            return 1;
        }
        else if(!strcmp(token, "room_info"))
        {
            room_p r = engine_camera.current_room;
            if(r)
            {
                room_sector_p sect = Room_GetSectorXYZ(r, engine_camera.pos);
                Con_Printf("ID = %d, x_sect = %d, y_sect = %d", r->id, r->sectors_x, r->sectors_y);
                if(sect)
                {
                    Con_Printf("sect(%d, %d), inpenitrable = %d, r_up = %d, r_down = %d", sect->index_x, sect->index_y,
                               (int)(sect->ceiling == TR_METERING_WALLHEIGHT || sect->floor == TR_METERING_WALLHEIGHT), (int)(sect->sector_above != NULL), (int)(sect->sector_below != NULL));
                    for(uint32_t i=0;i<sect->owner_room->content->static_mesh_count;i++)
                    {
                        Con_Printf("static[%d].object_id = %d", i, sect->owner_room->content->static_mesh[i].object_id);
                    }
                    for(engine_container_p cont=sect->owner_room->content->containers;cont;cont=cont->next)
                    {
                        if(cont->object_type == OBJECT_ENTITY)
                        {
                            entity_p e = (entity_p)cont->object;
                            Con_Printf("cont[entity](%d, %d, %d).object_id = %d", (int)e->transform[12+0], (int)e->transform[12+1], (int)e->transform[12+2], e->id);
                        }
                    }
                }
            }
            return 1;
        }
        else if(!strcmp(token, "xxx"))
        {
            FILE *f = fopen("ascII.txt", "r");
            if(f)
            {
                long int size;
                char *buf;
                fseek(f, 0, SEEK_END);
                size= ftell(f);
                buf = (char*) malloc((size+1)*sizeof(char));

                fseek(f, 0, SEEK_SET);
                fread(buf, size, sizeof(char), f);
                buf[size] = 0;
                fclose(f);
                Con_Clean();
                Con_AddText(buf, FONTSTYLE_CONSOLE_INFO);
                free(buf);
            }
            else
            {
                Con_AddText("Not avaliable =(", FONTSTYLE_CONSOLE_WARNING);
            }
            return 1;
        }
        else if(token[0])
        {
            if(engine_lua)
            {
                Con_AddLine(pch, FONTSTYLE_GENERIC);
                if (luaL_dostring(engine_lua, pch) != LUA_OK)
                {
                    Con_AddLine(lua_tostring(engine_lua, -1), FONTSTYLE_CONSOLE_WARNING);
                    lua_pop(engine_lua, 1);
                }
            }
            else
            {
                char buf[1024];
                snprintf(buf, 1024, "Command \"%s\" not found", token);
                Con_AddLine(buf, FONTSTYLE_CONSOLE_WARNING);
            }
            return 0;
        }
    }

    return 0;
}
