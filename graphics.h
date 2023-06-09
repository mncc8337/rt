#pragma once
#include <SDL2/SDL.h>
#include "vec3.h"
#include "constant.h"
#include "objects.h"
#include "camera.h"
#include "transformation.h"

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_sdl2.h"
#include "imgui/backends/imgui_impl_sdlrenderer2.h"

#include <vector>
#include <iostream>

#include <sstream>
#include <iomanip>

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

class SDL {
private:
    SDL_Texture* texture;
    ImGuiIO io;

    Object* prev_object = nullptr;

    // object transform property
    float position[3];
    float rotation[3];
    float scale[3];
    float radius;
    bool uniform_scaling = true;

    // object material property
    float color[3];
    float emission_color[3];
    float emission_strength = 0.0f;
    float roughness = 1.0f;
    float specular_color[3];
    float metal = 1.0f;
    bool transparent = false;
    float refractive_index = 0;
    const char* refractive_index_items[6] = {"air", "water", "glass", "flint glass", "diamond", "self-define"};
    int refractive_index_current_item = 1;
    bool smoke = false;
    float density = 1.0f;

    // editor setting
    bool show_crosshair = false;
    float up_sky_color[3] = {0.5, 0.7, 1.0};
    float down_sky_color[3] = {1.0, 1.0, 1.0};
    float gamma = 1.0f;

    Object* focal_plane = nullptr;
    bool show_focal_plane = false;

public:
    SDL_Event event;
    SDL_Window *window;
    SDL_Renderer *renderer;

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

        // setup imgui
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        io = ImGui::GetIO(); (void)io;

        ImGui::StyleColorsDark();

        ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
        ImGui_ImplSDLRenderer2_Init(renderer);
    }
    bool is_hover_over_gui() {
        return ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow | ImGuiHoveredFlags_AllowWhenBlockedByPopup);
    }
    void change_geometry(int w, int h) {
        WIDTH = w;
        HEIGHT = h;
        disable_drawing();
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT);
    }
    void save_image(std::vector<std::vector<Vec3>>* screen_color, int tonemapping_method, float gamma) {
        unsigned char data[WIDTH * HEIGHT * 3];

        for(int x = 0; x < WIDTH; x++)
            for(int y = 0; y < HEIGHT; y++) {
                Vec3 color = (*screen_color)[x][y];
                color = tonemap(color, RGB_CLAMPING);
                color = gamma_correct(color, gamma);
                color *= 255;

                int r = int(color.x);
                int g = int(color.y);
                int b = int(color.z);

                data[(y * WIDTH + x) * 3 + 0] = r;
                data[(y * WIDTH + x) * 3 + 1] = g;
                data[(y * WIDTH + x) * 3 + 2] = b;
            }     
        auto t = std::time(nullptr);
        auto tm = *std::localtime(&t);

        std::string str;
        std::ostringstream oss;
        oss << std::put_time(&tm, "%d-%m-%Y-%H-%M-%S");
        str = "res/" + oss.str() + ".png";
        char *c = const_cast<char*>(str.c_str());

        stbi_write_png(c, WIDTH, HEIGHT, 3, data, WIDTH * 3);
    }
    void process_gui_event() {
        ImGui_ImplSDL2_ProcessEvent(&event);
    }
    void gui(std::vector<std::vector<Vec3>>* screen,
             bool* lazy_ray_trace, int* frame_count, int* frame_num, double delay,
             int* width, int* height,
             std::vector<Object*>* oc, Object* selecting_object,
             bool* make_sphere_request, bool* make_mesh_request, std::string* request_mesh_name,
             void (*remove_object_func)(Object*),
             Camera* camera,
             Vec3* up_sky_c, Vec3* down_sky_c,
             bool* running) {
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // copy all pixel to renderer
        enable_drawing();
        for(int x = 0; x < WIDTH; x++)
            for(int y = 0; y < HEIGHT; y++) {
                // post processing
                Vec3 COLOR = tonemap((*screen)[x][y], RGB_CLAMPING);
                COLOR = gamma_correct(COLOR, gamma);

                draw_pixel(x, y, COLOR);
            }
        disable_drawing();
        
        if(ImGui::CollapsingHeader("Editor")) {
            std::string info = "done rendering";
            if(*frame_num + 1 < *frame_count)
                info = "rendering frame " + std::to_string(*frame_num + 1) + '/' + std::to_string(*frame_count);

            std::string delay_text = "last frame delay " + std::to_string(delay) + "ms";

            ImGui::Text("%s", info.c_str());
            ImGui::Text("%s", delay_text.c_str());

            ImGui::InputInt("viewport width", width, 1);
            *width = fmin(*width, MAX_WIDTH);
            *width = fmax(*width, 2);

            ImGui::InputInt("viewport height", height, 1);
            *height = fmin(*height, MAX_HEIGHT);
            *height = fmax(*height, 2);

            ImGui::Checkbox("lazy ray tracing", lazy_ray_trace);
            if(ImGui::IsItemHovered())
                ImGui::SetTooltip("increase performance but decrease image quality");

            ImGui::Checkbox("show crosshair", &show_crosshair);

            bool old_show_focal_plane = show_focal_plane;
            ImGui::Checkbox("show focal plane", &show_focal_plane);
            if(show_focal_plane) {
                // add focal plane if not have
                if(focal_plane == nullptr) {
                    focal_plane = (*oc)[0];
                }
                // show the focal plane
                focal_plane->visible = true;

                Material mat;
                mat.color = BLACK;
                mat.emission_color = WHITE;
                mat.emission_strength = 0.1f;

                Vec3 look_dir = camera->get_looking_direction();
                Vec3 new_pos = camera->position + look_dir * camera->focal_length;
                // get perpendicular vector of camera looking dir
                Vec3 rotating_axis = look_dir.cross({0, 1, 0}).normalize();

                focal_plane->tris = focal_plane->default_tris;
                for(int i = 0; i < (int)focal_plane->tris.size(); i++) {
                    for(int j = 0; j < 3; j++) {
                        Vec3* pos = &(focal_plane->tris[i].vert[j]);
                        *pos = _scale(*pos, {1e3, 0, 1e3});
                        *pos = _rotate_y(*pos, camera->panned_angle); // panned angle
                        *pos = _rotate_on_axis(*pos, rotating_axis, camera->tilted_angle + M_PI / 2); // tilted angle
                        *pos = *pos + new_pos;
                    }
                    focal_plane->tris[i].material = mat;
                }
                // calculate AABB for rendering
                focal_plane->calculate_AABB();
            }
            else if(focal_plane != nullptr) {
                // hide the focal plane object
                focal_plane->visible = false;
            }
            // force render if clicked
            if(old_show_focal_plane != show_focal_plane)
                *frame_num = 0;

            ImGui::ColorEdit3("up sky color", up_sky_color);
            Vec3 ukc = Vec3(up_sky_color[0], up_sky_color[1], up_sky_color[2]);
            if(*up_sky_c != ukc)
                *up_sky_c = ukc;

            ImGui::ColorEdit3("down sky color", down_sky_color);
            Vec3 dkc = Vec3(down_sky_color[0], down_sky_color[1], down_sky_color[2]);
            if(*down_sky_c != dkc)
                *down_sky_c = dkc;

            ImGui::InputInt("frame count", frame_count, 1);
            if(ImGui::IsItemHovered())
                ImGui::SetTooltip("number of frame will be rendered");

            if(ImGui::Button("render"))
                *frame_num = 0;
            ImGui::SameLine();
            if(ImGui::Button("stop render"))
                *frame_num = *frame_count;

            if(ImGui::Button("fit window size with viewport size")) SDL_SetWindowSize(window, *width, *height);
            if(ImGui::Button("save image")) save_image(screen, RGB_CLAMPING, gamma);
            ImGui::SameLine();
            if(ImGui::Button("quit"))
                *running = false;
        }

        if(ImGui::CollapsingHeader("camera")) {
            ImGui::DragFloat("gamma correction", &gamma, 0.01f, 0.0f, INFINITY, "%.3f", ImGuiSliderFlags_AlwaysClamp);

            ImGui::SliderFloat("FOV", &(camera->FOV), 0, 180);
            ImGui::DragFloat("focal length", &(camera->focal_length), 0.1f, 0.0f, INFINITY, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::DragFloat("max range", &(camera->max_range), 1, 0.0f, INFINITY, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::DragFloat("blur rate", &(camera->blur_rate), 0.001f, 0.0f, INFINITY, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::InputInt("max ray bounce", &(camera->max_ray_bounce_count), 1);
            camera->max_ray_bounce_count = fmax(camera->max_ray_bounce_count, 1);

            ImGui::InputInt("ray per pixel", &(camera->ray_per_pixel), 1);
            camera->ray_per_pixel = fmax(camera->ray_per_pixel, 1);
        }

        ImGui::Begin("object property");
        if(selecting_object == nullptr) {
            if(ImGui::Button("add sphere")) {
                *make_sphere_request = true;
            }
            ImGui::SameLine();
            if(ImGui::Button("add plane")) {
                *make_mesh_request = true;
                *request_mesh_name = "default_model/plane.obj";
            }
            ImGui::SameLine();
            if(ImGui::Button("add cube")) {
                *make_mesh_request = true;
                *request_mesh_name = "default_model/cube.obj";
            }
            ImGui::SameLine();
            if(ImGui::Button("add dodecahedron")) {
                *make_mesh_request = true;
                *request_mesh_name = "default_model/dodecahedron.obj";
            }
        }
        else if(selecting_object == focal_plane) {
            ImGui::Text("selecting focal plane");
        }
        else {
            const char* typ;
            if(selecting_object->is_sphere())
                typ = std::string("sphere").c_str();
            else
                typ = std::string("mesh").c_str();
            ImGui::Text("selecting a %s", typ);

            // if select a new object
            if(prev_object != selecting_object) {
                Vec3 pos = selecting_object->get_position();
                Vec3 rot = selecting_object->get_rotation();
                Vec3 scl = selecting_object->get_scale();
                float rad = selecting_object->get_radius();
                Material mat = selecting_object->get_material();

                position[0] = pos.x;
                position[1] = pos.y;
                position[2] = pos.z;

                rotation[0] = rad2deg(rot.x);
                rotation[1] = rad2deg(rot.y);
                rotation[2] = rad2deg(rot.z);

                scale[0] = scl.x;
                scale[1] = scl.y;
                scale[2] = scl.z;

                radius = rad;
                
                color[0] = mat.color.x;
                color[1] = mat.color.y;
                color[2] = mat.color.z;

                emission_color[0] = mat.emission_color.x;
                emission_color[1] = mat.emission_color.y;
                emission_color[2] = mat.emission_color.z;
                emission_strength = mat.emission_strength;

                roughness = mat.roughness;
                metal = mat.metal;

                specular_color[0] = mat.specular_color.x;
                specular_color[1] = mat.specular_color.y;
                specular_color[2] = mat.specular_color.z;

                transparent = mat.transparent;
                refractive_index = mat.refractive_index;
            }
            prev_object = selecting_object;

            ImGui::Text("transform");
            ImGui::DragFloat3("position", position, 0.1f);
            ImGui::DragFloat3("rotation", rotation, 1.0f);
            if(selecting_object->is_sphere()) {
                ImGui::DragFloat("radius", &radius, 0.5f);
            }
            else {
                ImGui::DragFloat3("scaling", scale, 0.1f);
                ImGui::Checkbox("uniform scaling", &uniform_scaling);
            }
            ImGui::Text("material");
            ImGui::ColorEdit3("color", color);
            if(ImGui::IsItemHovered())
                ImGui::SetTooltip("inner coat color (light can only diffuse if not transparency)");
            ImGui::ColorEdit3("emission color", emission_color);
            ImGui::DragFloat("emission_strength", &emission_strength, 0.1f, 0.0f, INFINITY, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat("roughness", &roughness, 0, 1);
            if(ImGui::IsItemHovered())
                ImGui::SetTooltip("how rough the inner coat is");
            ImGui::SliderFloat("metal", &metal, 0, 1);
            if(ImGui::IsItemHovered())
                ImGui::SetTooltip("how hard the outer coat is to be penetrated");
            ImGui::ColorEdit3("specular color", specular_color);
            if(ImGui::IsItemHovered())
                ImGui::SetTooltip("outer coat color (light can pass through and be tinted)\ncontrolled through metal property\nmimic the property of plastics, fruits,...");
            ImGui::Checkbox("transparent", &transparent);
            if(transparent) {
                ImGui::Combo("refractive index", &refractive_index_current_item, refractive_index_items, 6);
                switch(refractive_index_current_item) {
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
                        ImGui::InputFloat(" ", &refractive_index, 0.01f);
                        break;
                }
            }
            ImGui::Checkbox("smoke", &smoke);
            if(smoke)
                ImGui::SliderFloat("smoke density", &density, 0, 1);

            if(uniform_scaling) {
                int difference_count = (scale[0] != scale[1]) + (scale[1] != scale[2]) + (scale[0] != scale[2]);
                float MAX = fmax(scale[0], fmax(scale[1], scale[2]));
                float MIN = fmin(scale[0], fmin(scale[1], scale[2]));
                int max_count = 0;
                for(int i = 0; i < 3; i++)
                    if(scale[i] == MAX) max_count++;

                if(difference_count == 3) {
                    scale[0] = MAX;
                    scale[1] = MAX;
                    scale[2] = MAX;
                }
                else {
                    if(max_count == 2) {
                        scale[0] = MIN;
                        scale[1] = MIN;
                        scale[2] = MIN;
                    }
                    else {
                        scale[0] = MAX;
                        scale[1] = MAX;
                        scale[2] = MAX;
                    }
                }
            }

            Vec3 new_pos = Vec3(position[0], position[1], position[2]);
            Vec3 new_rot = Vec3(deg2rad(rotation[0]), deg2rad(rotation[1]), deg2rad(rotation[2]));
            Vec3 new_scl = Vec3(scale[0], scale[1], scale[2]);
            Vec3 new_color = Vec3(color[0], color[1], color[2]);
            Vec3 new_emission_color = Vec3(emission_color[0], emission_color[1], emission_color[2]);
            Vec3 new_specular_color = Vec3(specular_color[0], specular_color[1], specular_color[2]);

            Object* obj = selecting_object;
            Material mat = obj->get_material();
            bool scale_changed;
            if(obj->is_sphere())
                scale_changed = obj->get_radius() != radius;
            else
                scale_changed = obj->get_scale() != new_scl;

            bool object_changed = obj->get_position() != new_pos
                                    or obj->get_rotation() != new_rot
                                    or scale_changed
                                    or mat.color != new_color
                                    or mat.emission_color != new_emission_color
                                    or mat.emission_strength != emission_strength
                                    or mat.roughness != roughness
                                    or mat.metal != metal
                                    or mat.specular_color != new_specular_color
                                    or mat.transparent != transparent
                                    or mat.refractive_index != refractive_index
                                    or mat.smoke != smoke
                                    or mat.density != density;
            if(object_changed) {
                if(obj->is_sphere())
                    obj->set_radius(radius);
                else
                    obj->set_scale(new_scl);
                obj->set_rotation(new_rot);
                obj->set_position(new_pos);
                mat.color = new_color;
                mat.emission_color = new_emission_color;
                mat.emission_strength = emission_strength;
                mat.roughness = roughness;
                mat.metal = metal;
                mat.specular_color = new_specular_color;
                mat.transparent = transparent;
                mat.refractive_index = refractive_index;
                mat.smoke = smoke;
                mat.density = density;
                obj->set_material(mat);
                obj->calculate_AABB();
                *frame_num = 0;
            }
            if(ImGui::Button("delete object")) {
                remove_object_func(selecting_object);
                selecting_object = nullptr;
                *frame_num = 0;
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
        pixels[y * pitch + x * 4 + 0] = color.z * 255;
        pixels[y * pitch + x * 4 + 1] = color.y * 255;
        pixels[y * pitch + x * 4 + 2] = color.x * 255;
        pixels[y * pitch + x * 4 + 3] = 255;
    }
    void disable_drawing() {
        SDL_UnlockTexture(texture);
    }
    void load_texture() {
        // scale the texture to fit the window
        SDL_Rect rect;
        rect.x = 0; rect.y = 0;
        SDL_GetWindowSize(window, &(rect.w), &(rect.h));
        // load texture to renderer
        SDL_RenderCopy(renderer, texture, NULL, &rect);
    }
    void draw_crosshair() {
        int w, h;
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
