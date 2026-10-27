#include "../../include/Tray/TrayXEmbed.hpp"
#include "../../include/Tray/TrayPopupMenu.hpp"

#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrender.h>

#include <algorithm>
#include <math.h>
#include <stdio.h>

namespace gsr {
    static constexpr long SYSTEM_TRAY_REQUEST_DOCK = 0;
    static constexpr long XEMBED_MAPPED = 1 << 0;
    static constexpr int default_icon_size = 24;

    static bool is_little_endian() {
        const uint16_t value = 1;
        return *(const uint8_t*)&value == 1;
    }

    static std::vector<uint32_t> scale_to_premultiplied_argb(const TrayPixmap &pixmap, int size) {
        std::vector<uint32_t> result((size_t)size * (size_t)size);
        const float scale_x = (float)pixmap.width / (float)size;
        const float scale_y = (float)pixmap.height / (float)size;

        for(int y = 0; y < size; ++y) {
            const float src_y = std::max(0.0f, (y + 0.5f) * scale_y - 0.5f);
            const int y0 = std::min((int)src_y, pixmap.height - 1);
            const int y1 = std::min(y0 + 1, pixmap.height - 1);
            const float fy = src_y - (float)y0;

            for(int x = 0; x < size; ++x) {
                const float src_x = std::max(0.0f, (x + 0.5f) * scale_x - 0.5f);
                const int x0 = std::min((int)src_x, pixmap.width - 1);
                const int x1 = std::min(x0 + 1, pixmap.width - 1);
                const float fx = src_x - (float)x0;

                const int sample_x[4] = { x0, x1, x0, x1 };
                const int sample_y[4] = { y0, y0, y1, y1 };
                const float sample_weight[4] = { (1.0f - fx) * (1.0f - fy), fx * (1.0f - fy), (1.0f - fx) * fy, fx * fy };

                float argb[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                for(int i = 0; i < 4; ++i) {
                    const unsigned char *src = pixmap.argb_data.data() + ((size_t)sample_y[i] * pixmap.width + sample_x[i]) * 4;
                    const float alpha = src[0] / 255.0f;
                    argb[0] += src[0] * sample_weight[i];
                    argb[1] += src[1] * alpha * sample_weight[i];
                    argb[2] += src[2] * alpha * sample_weight[i];
                    argb[3] += src[3] * alpha * sample_weight[i];
                }

                uint32_t pixel = 0;
                for(int i = 0; i < 4; ++i) {
                    pixel = (pixel << 8) | (uint32_t)std::clamp((int)lroundf(argb[i]), 0, 255);
                }
                result[(size_t)y * size + x] = pixel;
            }
        }
        return result;
    }

    TrayXEmbed::TrayXEmbed() = default;

    TrayXEmbed::~TrayXEmbed() {
        if(!dpy)
            return;

        popup_menu.reset();
        destroy_icon_window();
        XCloseDisplay(dpy);
    }

    bool TrayXEmbed::init(const TrayPixmap *pixmap) {
        this->pixmap = pixmap;

        dpy = XOpenDisplay(nullptr);
        if(!dpy) {
            fprintf(stderr, "Error: TrayXEmbed::init: failed to connect to the X server\n");
            return false;
        }

        int render_event_base = 0;
        int render_error_base = 0;
        if(!XRenderQueryExtension(dpy, &render_event_base, &render_error_base)) {
            fprintf(stderr, "Error: TrayXEmbed::init: the X server doesn't support the render extension\n");
            XCloseDisplay(dpy);
            dpy = nullptr;
            return false;
        }

        char selection_name[64];
        snprintf(selection_name, sizeof(selection_name), "_NET_SYSTEM_TRAY_S%d", DefaultScreen(dpy));
        selection_atom = XInternAtom(dpy, selection_name, False);
        opcode_atom = XInternAtom(dpy, "_NET_SYSTEM_TRAY_OPCODE", False);
        manager_atom = XInternAtom(dpy, "MANAGER", False);
        visual_atom = XInternAtom(dpy, "_NET_SYSTEM_TRAY_VISUAL", False);
        xembed_info_atom = XInternAtom(dpy, "_XEMBED_INFO", False);

        XSelectInput(dpy, DefaultRootWindow(dpy), StructureNotifyMask);
        find_tray_manager();
        return true;
    }

    void TrayXEmbed::poll() {
        if(!dpy)
            return;

        XEvent xev;
        while(XPending(dpy)) {
            XNextEvent(dpy, &xev);
            handle_event(xev);
        }

        if(popup_menu && popup_menu->is_closed()) {
            if(!pending_action)
                pending_action = popup_menu->get_selected_action();
            popup_menu.reset();
            XFlush(dpy);
        }

        const std::optional<TrayAction> action = pending_action;
        pending_action.reset();
        if(action && on_action)
            on_action(*action);
    }

    void TrayXEmbed::set_pixmap(const TrayPixmap *pixmap) {
        this->pixmap = pixmap;
        draw();
    }

    void TrayXEmbed::handle_event(XEvent &xev) {
        if(popup_menu && xev.xany.window == popup_menu->get_window()) {
            popup_menu->handle_event(xev);
            return;
        }

        switch(xev.type) {
            case ClientMessage: {
                if(xev.xclient.window == DefaultRootWindow(dpy) && xev.xclient.message_type == manager_atom
                    && (Atom)xev.xclient.data.l[1] == selection_atom && (Window)xev.xclient.data.l[2] != manager_window)
                {
                    find_tray_manager();
                }
                break;
            }
            case DestroyNotify: {
                if(xev.xdestroywindow.window == manager_window) {
                    popup_menu.reset();
                    destroy_icon_window();
                    manager_window = None;
                } else if(xev.xdestroywindow.window == icon_window) {
                    icon_window = None;
                }
                break;
            }
            case ConfigureNotify: {
                if(xev.xconfigure.window == icon_window) {
                    icon_width = xev.xconfigure.width;
                    icon_height = xev.xconfigure.height;
                    draw();
                }
                break;
            }
            case Expose: {
                if(xev.xexpose.window == icon_window && xev.xexpose.count == 0)
                    draw();
                break;
            }
            case ButtonPress: {
                if(xev.xbutton.window == icon_window && xev.xbutton.button == Button3) {
                    popup_menu = std::make_unique<TrayPopupMenu>(dpy);
                    if(!popup_menu->open(xev.xbutton.x_root, xev.xbutton.y_root))
                        popup_menu.reset();
                }
                break;
            }
            case ButtonRelease: {
                const bool inside_icon = xev.xbutton.x >= 0 && xev.xbutton.x < icon_width && xev.xbutton.y >= 0 && xev.xbutton.y < icon_height;
                if(xev.xbutton.window == icon_window && xev.xbutton.button == Button1 && inside_icon)
                    pending_action = TrayAction::SHOW_UI;
                break;
            }
        }
    }

    void TrayXEmbed::find_tray_manager() {
        XGrabServer(dpy);
        manager_window = XGetSelectionOwner(dpy, selection_atom);
        if(manager_window != None)
            XSelectInput(dpy, manager_window, StructureNotifyMask);
        XUngrabServer(dpy);
        XFlush(dpy);

        if(manager_window != None)
            dock();
        else
            destroy_icon_window();
    }

    void TrayXEmbed::get_tray_visual(Visual **visual, int *depth) {
        *visual = DefaultVisual(dpy, DefaultScreen(dpy));
        *depth = DefaultDepth(dpy, DefaultScreen(dpy));

        Atom type = None;
        int format = 0;
        unsigned long num_items = 0;
        unsigned long bytes_after = 0;
        unsigned char *data = nullptr;
        if(XGetWindowProperty(dpy, manager_window, visual_atom, 0, 1, False, XA_VISUALID, &type, &format, &num_items, &bytes_after, &data) != Success || !data)
            return;

        const VisualID visual_id = (type == XA_VISUALID && format == 32 && num_items == 1) ? (VisualID)*(unsigned long*)data : 0;
        XFree(data);
        if(visual_id == 0)
            return;

        XVisualInfo visual_info_template;
        visual_info_template.visualid = visual_id;
        int num_visual_infos = 0;
        XVisualInfo *visual_info = XGetVisualInfo(dpy, VisualIDMask, &visual_info_template, &num_visual_infos);
        if(!visual_info)
            return;

        *visual = visual_info->visual;
        *depth = visual_info->depth;
        XFree(visual_info);
    }

    void TrayXEmbed::dock() {
        destroy_icon_window();

        Visual *visual = nullptr;
        int depth = 0;
        get_tray_visual(&visual, &depth);

        const Window root_window = DefaultRootWindow(dpy);
        const bool has_alpha = depth == 32;

        XSetWindowAttributes window_attr = {};
        unsigned long window_attr_mask = CWEventMask | CWBorderPixel | CWColormap;
        window_attr.event_mask = ExposureMask | StructureNotifyMask | ButtonPressMask | ButtonReleaseMask;
        window_attr.border_pixel = 0;
        if(has_alpha) {
            icon_colormap = XCreateColormap(dpy, root_window, visual, AllocNone);
            window_attr.colormap = icon_colormap;
            window_attr.background_pixel = 0;
            window_attr_mask |= CWBackPixel;
        } else {
            window_attr.colormap = DefaultColormap(dpy, DefaultScreen(dpy));
            window_attr.background_pixmap = ParentRelative;
            window_attr_mask |= CWBackPixmap;
        }

        icon_width = default_icon_size;
        icon_height = default_icon_size;
        icon_visual = visual;
        icon_window = XCreateWindow(dpy, root_window, 0, 0, icon_width, icon_height, 0, depth, InputOutput, visual, window_attr_mask, &window_attr);
        if(!icon_window) {
            fprintf(stderr, "Error: TrayXEmbed: failed to create the tray icon window\n");
            destroy_icon_window();
            return;
        }

        char class_name[] = "gsr-ui";
        XClassHint class_hint = { class_name, class_name };
        XSetClassHint(dpy, icon_window, &class_hint);
        XStoreName(dpy, icon_window, "GPU Screen Recorder");

        const long xembed_info[2] = { 0, XEMBED_MAPPED };
        XChangeProperty(dpy, icon_window, xembed_info_atom, xembed_info_atom, 32, PropModeReplace, (const unsigned char*)xembed_info, 2);

        XEvent xev = {};
        xev.xclient.type = ClientMessage;
        xev.xclient.window = manager_window;
        xev.xclient.message_type = opcode_atom;
        xev.xclient.format = 32;
        xev.xclient.data.l[0] = CurrentTime;
        xev.xclient.data.l[1] = SYSTEM_TRAY_REQUEST_DOCK;
        xev.xclient.data.l[2] = icon_window;
        XSendEvent(dpy, manager_window, False, NoEventMask, &xev);
        XFlush(dpy);
    }

    void TrayXEmbed::destroy_icon_window() {
        if(icon_window) {
            XDestroyWindow(dpy, icon_window);
            icon_window = None;
        }

        if(icon_colormap) {
            XFreeColormap(dpy, icon_colormap);
            icon_colormap = None;
        }

        icon_visual = nullptr;
        XFlush(dpy);
    }

    void TrayXEmbed::draw() {
        if(!icon_window)
            return;

        XClearWindow(dpy, icon_window);

        const int icon_size = std::min(icon_width, icon_height);
        if(!pixmap || pixmap->argb_data.empty() || icon_size <= 0) {
            XFlush(dpy);
            return;
        }

        std::vector<uint32_t> pixels = scale_to_premultiplied_argb(*pixmap, icon_size);
        XImage *image = XCreateImage(dpy, DefaultVisual(dpy, DefaultScreen(dpy)), 32, ZPixmap, 0, (char*)pixels.data(), icon_size, icon_size, 32, icon_size * 4);
        if(!image)
            return;
        image->byte_order = is_little_endian() ? LSBFirst : MSBFirst;

        const Pixmap source_pixmap = XCreatePixmap(dpy, icon_window, icon_size, icon_size, 32);
        const GC gc = XCreateGC(dpy, source_pixmap, 0, nullptr);
        XPutImage(dpy, source_pixmap, gc, image, 0, 0, 0, 0, icon_size, icon_size);
        image->data = nullptr;
        XDestroyImage(image);

        const Picture source_picture = XRenderCreatePicture(dpy, source_pixmap, XRenderFindStandardFormat(dpy, PictStandardARGB32), 0, nullptr);
        const Picture window_picture = XRenderCreatePicture(dpy, icon_window, XRenderFindVisualFormat(dpy, icon_visual), 0, nullptr);
        XRenderComposite(dpy, PictOpOver, source_picture, None, window_picture, 0, 0, 0, 0,
            (icon_width - icon_size) / 2, (icon_height - icon_size) / 2, icon_size, icon_size);

        XRenderFreePicture(dpy, window_picture);
        XRenderFreePicture(dpy, source_picture);
        XFreeGC(dpy, gc);
        XFreePixmap(dpy, source_pixmap);
        XFlush(dpy);
    }
}
