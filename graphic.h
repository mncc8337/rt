// this file contain method to create window and draw pixel to it

#pragma once
#include <SDL2/SDL.h>
#include "vec3.h"
#include "constant.h"
#include "helper.h"
#include "objects.h"
#include "camera.h"

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_sdl2.h"
#include "imgui/backends/imgui_impl_sdlrenderer2.h"

#include <SDL2/SDL_render.h>
#include <SDL2/SDL_video.h>
#include <vector>
#include <iostream>
#include <sstream>
#include <iomanip>
#include "CImg.h"

using namespace cimg_library;

class SDL {
private:
    std::vector<std::vector<Vec3>> screen_color;
    SDL_Renderer *renderer;
    SDL_Texture* texture;
    ImGuiIO io;

    double prev_delay = 0;
    int prev_object_id = -1;
    std::string prev_object_type;

    // object transform property
    float position[3];
    float radius;

    // object material property
    float color[3];
    float emission_color[3];
    float emission_strength = 0.0f;
    float roughness = 1;
    bool transparent = false;
    float refractive_index = 0;
    int current_item = 1;   // used on refractive index combo box

    // camera property
    float FOV;
    float focal_length;
    int max_ray_bounce_count;
    int ray_per_pixel;
    float max_range;

    // editor setting
    bool show_crosshair = false;
    float up_sky_color[3] = {0.5, 0.7, 1.0};
    float down_sky_color[3] = {1.0, 1.0, 1.0};

public:
    SDL_Event event;
    SDL_Window *window;

    int WIDTH;
    int HEIGHT;

    unsigned char* pixels;
    int pitch;

    SDL(char* title, int w, int h) {
        WIDTH = w; HEIGHT = h;
        SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER);

        window = SDL_CreateWindow(title,
                              SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              1280, 720,
                              SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);

        screen_color = v_screen;

        // setup imgui
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        io = ImGui::GetIO(); (void)io;

        ImGui::StyleColorsDark();

        ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
        ImGui_ImplSDLRenderer2_Init(renderer);
    }
    bool is_hover_over_gui() {
        return ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);
    }
    void load_texture() {
        // load texture to renderer
        SDL_Rect rect;
        rect.x = 0; rect.y = 0;
        int w; int h;
        SDL_GetWindowSize(window, &w, &h);
        // scale the texture to fit the window
        rect.w = w; rect.h = h;
        SDL_RenderCopy(renderer, texture, NULL, &rect);
    }
    void change_geometry(int w, int h) {
        WIDTH = w;
        HEIGHT = h;
        disable_drawing();
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT);
    }
    void save_image() {
        CImg<int> image(WIDTH, HEIGHT, 1, 3);
        for(int x = 0; x < WIDTH; x++)
            for(int y = 0; y < HEIGHT; y++) {
                Vec3 c = screen_color[x][y];
                int COLOR[] = {int(c.x * 255), int(c.y * 255), int(c.z * 255)};
                image.draw_point(x, y, COLOR, 1.0f);
            }
        
        auto t = std::time(nullptr);
        auto tm = *std::localtime(&t);

        std::ostringstream oss;
        oss << std::put_time(&tm, "%d-%m-%Y-%H-%M-%S");
        auto str = "res/" + oss.str() + ".bmp";
        char *c = const_cast<char*>(str.c_str());

        image.save(c);
    }
    void process_gui_event() {
        ImGui_ImplSDL2_ProcessEvent(&event);
    }
    void set_camera_var(Camera* camera) {
        FOV = (*camera).FOV;
        focal_length = (*camera).focal_length;
        max_range = (*camera).max_range;
        max_ray_bounce_count = (*camera).max_ray_bounce_count;
        ray_per_pixel = (*camera).ray_per_pixel;
    }
    void gui(bool* lazy_ray_trace, int* frame_count, float* blur_rate, bool* force_rerender, int* frame_num, double delay, int* width, int* height, ObjectContainer* oc, Camera* camera, void (*camera_update_func)(void), Vec3* up_sky_c, Vec3* down_sky_c, int* selecting_object, std::string* selecting_object_type) {
        //GUI part
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        
        if(ImGui::CollapsingHeader("Editor")) {
            std::string info = "done rendering";
            if(*frame_num+1 < *frame_count)
                info = "rendering frame " + std::to_string(*frame_num + 1) + '/' + std::to_string(*frame_count);

            std::string delay_text = "delay " + std::to_string(delay) + "ms";
            if(delay == prev_delay) delay_text = "no work";
            prev_delay = delay;

            ImGui::Text("%s", info.c_str());
            ImGui::Text("%s", delay_text.c_str());

            ImGui::InputInt("viewport width", width, 1);
            if(*width > 2000) *width = 2000;
            if(*width <= 0) *width = 1;

            ImGui::InputInt("viewport height", height, 1);
            if(*height > 2000) *height = 2000;
            if(*height <= 0) *height = 1;

            ImGui::Checkbox("lazy ray tracing", lazy_ray_trace);
            if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("increase performance but decrease image quality");

            ImGui::Checkbox("show crosshair", &show_crosshair);

            ImGui::ColorEdit3("up sky color", up_sky_color);
            Vec3 ukc = Vec3(up_sky_color[0], up_sky_color[1], up_sky_color[2]);
            if(*up_sky_c != ukc)
                *up_sky_c = ukc;

            ImGui::ColorEdit3("down sky color", down_sky_color);
            Vec3 dkc = Vec3(down_sky_color[0], down_sky_color[1], down_sky_color[2]);
            if(*down_sky_c != dkc)
                    *down_sky_c = dkc;

            ImGui::InputInt("frame count", frame_count, 1);
            if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("number of frame will be rendered");

            if(ImGui::Button("render")) *force_rerender = true;
            ImGui::SameLine();
            if(ImGui::Button("stop render")) {
                *force_rerender = false;
                *frame_num = *frame_count;
            }
            if(ImGui::Button("fit window size with viewport size")) SDL_SetWindowSize(window, *width, *height);
            if(ImGui::Button("save image")) save_image();
        }

        if(ImGui::CollapsingHeader("camera")) {
            ImGui::SliderFloat("FOV", &FOV, 0, 180);
            ImGui::SliderFloat("focal length", &focal_length, 0.0001f, 100);
            ImGui::SliderFloat("max range", &max_range, 0.0001f, 1000);
            ImGui::InputFloat("blur rate", blur_rate, 0.001f);
            ImGui::InputInt("max ray bounce", &max_ray_bounce_count, 1);
            ImGui::InputInt("ray per pixel", &ray_per_pixel, 1);
            bool camera_changed = (*camera).FOV != FOV
                                  or (*camera).focal_length != focal_length
                                  or (*camera).max_range != max_range
                                  or (*camera).max_ray_bounce_count != max_ray_bounce_count
                                  or (*camera).ray_per_pixel != ray_per_pixel;
            if(camera_changed) {
                (*camera).FOV = FOV;
                (*camera).focal_length = focal_length;
                (*camera).max_range = max_range;
                (*camera).max_ray_bounce_count = max_ray_bounce_count;
                (*camera).ray_per_pixel = ray_per_pixel;
                camera_update_func();
            }
        }

        ImGui::Begin("object property");
        if(*selecting_object == -1) {
            if(ImGui::Button("add new sphere")) {
                Sphere sphere;
                int id = (*oc).add_sphere(sphere);
                *selecting_object = id;
                *selecting_object_type = "sphere";
                *force_rerender = true;
            }
        }
        else {
            const char* typ = (*selecting_object_type).c_str();
            ImGui::Text("selecting %s %d", typ, *selecting_object);

            // if select a new object
            if(prev_object_id != *selecting_object or prev_object_type != *selecting_object_type) {
                if(*selecting_object_type == "sphere") {
                    Vec3 centre = (*oc).spheres[*selecting_object].centre;
                    position[0] = centre.x;
                    position[1] = centre.y;
                    position[2] = centre.z;
                    radius = (*oc).spheres[*selecting_object].radius;
                    Material mat = (*oc).spheres[*selecting_object].material;
                    color[0] = mat.color.x;
                    color[1] = mat.color.y;
                    color[2] = mat.color.z;
                    emission_color[0] = mat.emission_color.x;
                    emission_color[1] = mat.emission_color.y;
                    emission_color[2] = mat.emission_color.z;
                    emission_strength = mat.emission_strength;
                    roughness = mat.roughness;
                    transparent = mat.transparent;
                    refractive_index = mat.refractive_index;
                }
            }
            prev_object_id = *selecting_object;
            prev_object_type = *selecting_object_type;

            ImGui::Text("transform");
            if(*selecting_object_type == "sphere") {
                ImGui::DragFloat3("position", position);
                ImGui::DragFloat("radius", &radius, 0.5f, 0.00001f, max_range + 15);
            }
            ImGui::Text("material");
            ImGui::ColorEdit3("color", color);
            ImGui::ColorEdit3("emission color", emission_color);
            ImGui::SliderFloat("emission_strength", &emission_strength, 0, 1);
            ImGui::SliderFloat("roughness", &roughness, 0, 1);
            ImGui::Checkbox("transparent", &transparent);
            if(transparent) {
                const char* items[] = {"air", "water", "glass", "flint glass", "diamond", "self-define"};
                ImGui::Combo("refractive index", &current_item, items, IM_ARRAYSIZE(items));
                switch(current_item) {
                    case 0:
                        refractive_index = RI_AIR;
                        break;
                    case 1:
                        refractive_index = RI_WATER;
                        break;
                    case 2:
                        refractive_index = RI_GLASS;
                        break;
                    case 3:
                        refractive_index = RI_FLINT_GLASS;
                        break;
                    case 4:
                        refractive_index = RI_DIAMOND;
                        break;
                    case 5:
                        ImGui::DragFloat(" ", &refractive_index, 0.01, 0.0f, 1000000.0f);
                        break;
                }
            }

            if(*selecting_object_type == "sphere") {
                Sphere* sphere = &((*oc).spheres[*selecting_object]);
                Material* mat = &((*sphere).material);
                bool object_changed = (*sphere).centre != Vec3(position[0], position[1], position[2])
                                      or (*sphere).radius != radius
                                      or (*mat).color != Vec3(color[0], color[1], color[2])
                                      or (*mat).emission_color != Vec3(emission_color[0], emission_color[1], emission_color[2])
                                      or (*mat).emission_strength != emission_strength
                                      or (*mat).roughness != roughness
                                      or (*mat).transparent != transparent
                                      or (*mat).refractive_index != refractive_index;
                if(object_changed) {
                    (*sphere).centre = {position[0], position[1], position[2]};
                    (*sphere).radius = radius;
                    (*mat).color = {color[0], color[1], color[2]};
                    (*mat).emission_color = {emission_color[0], emission_color[1], emission_color[2]};
                    (*mat).emission_strength = emission_strength;
                    (*mat).roughness = roughness;
                    (*mat).transparent = transparent;
                    (*mat).refractive_index = refractive_index;
                    (*sphere).calculate_aabb();
                    *force_rerender = true;
                }
            }
            if(ImGui::Button("delete object")) {
                (*oc).remove_sphere(*selecting_object);
                *selecting_object = -1;
                *force_rerender = true;
            }
        }
        ImGui::End();

        if(*width != WIDTH or *height != HEIGHT) change_geometry(*width, *height);
    }
    // graphics
    void enable_drawing() {
        SDL_LockTexture(texture, NULL, (void**)&pixels, &pitch);
    }
    void draw_pixel(int x, int y, Vec3 color) {
        screen_color[x][y] = color;
        pixels[y * pitch + x * 4 + 0] = color.z * 255;
        pixels[y * pitch + x * 4 + 1] = color.y * 255;
        pixels[y * pitch + x * 4 + 2] = color.x * 255;
        pixels[y * pitch + x * 4 + 3] = 255;
    }
    void disable_drawing() {
        SDL_UnlockTexture(texture);
    }
    void draw_crosshair() {
        int w; int h;
        SDL_GetWindowSize(window, &w, &h);
        int middle_x = w >> 1;
        int middle_y = h >> 1;
        SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
        for(int i = -!(w % 2); i <= 0; i++)
            SDL_RenderDrawLine(renderer, middle_x + i, middle_y - 5, middle_x + i, middle_y + 5 - !(h % 2));
        for(int i = -!(h % 2); i <= 0; i++)
            SDL_RenderDrawLine(renderer, middle_x - 5, middle_y + i, middle_x + 5 - !(w % 2), middle_y + i);
    }
    void render() {
        load_texture();
        if(show_crosshair) draw_crosshair();
        ImGui::Render();
        SDL_RenderSetScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
        SDL_RenderPresent(renderer);
    }
    void destroy() {
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();

        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
    }
};
