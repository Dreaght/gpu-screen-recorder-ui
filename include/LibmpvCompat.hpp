#pragma once

#include <stdint.h>

#if __has_include(<mpv/client.h>) && __has_include(<mpv/render.h>) && __has_include(<mpv/render_gl.h>)
extern "C" {
#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_gl.h>
}
#else
extern "C" {
    typedef struct mpv_handle mpv_handle;
    typedef struct mpv_render_context mpv_render_context;

    typedef enum mpv_format {
        MPV_FORMAT_NONE = 0,
        MPV_FORMAT_STRING = 1,
        MPV_FORMAT_OSD_STRING = 2,
        MPV_FORMAT_FLAG = 3,
        MPV_FORMAT_INT64 = 4,
        MPV_FORMAT_DOUBLE = 5,
        MPV_FORMAT_NODE = 6,
        MPV_FORMAT_NODE_ARRAY = 7,
        MPV_FORMAT_NODE_MAP = 8,
        MPV_FORMAT_BYTE_ARRAY = 9
    } mpv_format;

    typedef enum mpv_event_id {
        MPV_EVENT_NONE = 0,
        MPV_EVENT_SHUTDOWN = 1,
        MPV_EVENT_END_FILE = 7,
        MPV_EVENT_FILE_LOADED = 8,
        MPV_EVENT_PROPERTY_CHANGE = 22,
    } mpv_event_id;

    typedef enum mpv_end_file_reason {
        MPV_END_FILE_REASON_EOF = 0,
        MPV_END_FILE_REASON_STOP = 2,
        MPV_END_FILE_REASON_QUIT = 3,
        MPV_END_FILE_REASON_ERROR = 4,
        MPV_END_FILE_REASON_REDIRECT = 5,
    } mpv_end_file_reason;

    typedef struct mpv_event {
        mpv_event_id event_id;
        int error;
        uint64_t reply_userdata;
        void *data;
    } mpv_event;

    typedef struct mpv_event_end_file {
        mpv_end_file_reason reason;
        int error;
        int64_t playlist_entry_id;
        int64_t playlist_insert_id;
        int playlist_insert_num_entries;
    } mpv_event_end_file;

    typedef enum mpv_render_param_type {
        MPV_RENDER_PARAM_INVALID = 0,
        MPV_RENDER_PARAM_API_TYPE = 1,
        MPV_RENDER_PARAM_OPENGL_INIT_PARAMS = 2,
        MPV_RENDER_PARAM_OPENGL_FBO = 3,
        MPV_RENDER_PARAM_FLIP_Y = 4,
        MPV_RENDER_PARAM_X11_DISPLAY = 8,
        MPV_RENDER_PARAM_WL_DISPLAY = 9,
        MPV_RENDER_PARAM_ADVANCED_CONTROL = 10,
    } mpv_render_param_type;

    typedef struct mpv_render_param {
        mpv_render_param_type type;
        void *data;
    } mpv_render_param;

    #define MPV_RENDER_API_TYPE_OPENGL "opengl"

    typedef enum mpv_render_update_flag {
        MPV_RENDER_UPDATE_FRAME = 1 << 0,
    } mpv_render_context_flag;

    typedef struct mpv_opengl_init_params {
        void *(*get_proc_address)(void *ctx, const char *name);
        void *get_proc_address_ctx;
    } mpv_opengl_init_params;

    typedef struct mpv_opengl_fbo {
        int fbo;
        int w;
        int h;
        int internal_format;
    } mpv_opengl_fbo;
}
#endif
