#pragma once
/* Emerald Launcher 2.0 — the 3D shelf.

   Every game is a physical box standing on a glossy stage. A game with more
   than one version is shown as a fanned *stack* of boxes (one per version,
   each with its own cover), so you can tell at a glance — from any distance —
   whether there's something to choose. Three views:

     SHELF     cover-flow of games. A on a single-version game plays it;
               on a stack it opens the versions.
     VERSIONS  the stack fans out into its versions; pick one and play.
               (Sequential games show their weeks, locked ones greyed out.)
     INSPECT   pick up the focused box and turn it around: the back cover
               has the description, screenshots and version notes.

   The Launcher feeds it semantic inputs and turns its actions into launches. */
#include "boxshape.h"
#include "db.h"
#include "mat4.h"
#include "uikit.h"
#include <string>
#include <vector>

struct ShelfAction {
    enum Kind { NONE, LAUNCH, SETTINGS } kind = NONE;
    int group = -1;
    int entry = -1;
};

class Shelf {
public:
    bool init(void);                     /* GL setup; false → 3D unavailable */
    bool ready(void) const { return m_ready; }
    void attach(const std::vector<GameGroup> *groups);
    void select(int group);
    int  selected(void) const { return m_sel; }
    void set_front(int group, int entry);
    int  front(int group) const;

    void input(UiInput in);
    void set_right_stick(float x, float y);          /* -1..1 each */
    void mouse_button(float x, float y, int button, bool down);
    void mouse_motion(float x, float y);
    void mouse_wheel(int dy);

    void update(float dt);
    void draw_scene(void);
    void draw_overlay(PadStyle style, const std::string &app_name, bool modal_open);
    void draw_fade(void);

    bool poll_action(ShelfAction &out);
    void on_launched(void);              /* the launch request was handed off */
    void on_resume(void);                /* back in the launcher after a game  */
    void prewarm(double budget_ms);      /* build box art ahead of time         */
    void release_gpu(void);              /* free the big offscreen targets      */

private:
    enum View { V_SHELF, V_VERSIONS, V_INSPECT };

    struct Pose { float x, y, z, yaw, pitch, roll, scale, bright; };
    struct Slot { bool valid; int k; float x, z, yaw, bright; };
    /* One box of a stack, in the group's own frame: boxes line up on their
       right edges (each layer peeks out a little further) and stand one
       behind the other. */
    struct Layer { int j; const BoxShape *sh; float right, z, roll; };
    struct Inst {
        int   g, j;
        int   shape;                      /* boxshape id                       */
        const BoxShape *sh;
        int   layer;                      /* position in its stack (0 = front) */
        float kc;                         /* continuous shelf offset of its group */
        Pose  p;
        Mat4  model;
        float depth;
        unsigned atlas, back;
        bool  focus, locked;
        float lift;                       /* height of the box bottom above the floor */
        bool  on_screen;
        float sx0, sy0, sx1, sy1;         /* projected screen rect (1280x720) */
    };

    const std::vector<GameGroup> *m_groups = nullptr;
    bool  m_ready = false;
    View  m_view = V_SHELF;
    View  m_insp_from = V_SHELF;

    /* Shelf scroll: m_target is unbounded when the list wraps around. */
    int   m_sel = 0;
    int   m_target = 0;
    float m_scroll = 0.0f;
    std::vector<int> m_front;

    /* Versions view */
    int   m_vgroup = 0;
    int   m_ver = 0;
    float m_vscroll = 0.0f;
    bool  m_fan_static = true;            /* few versions: all side by side    */
    float m_fan_extra = 0.0f;             /* scrolling fan: widest box - 1 unit */
    std::vector<float> m_fan_x;           /* static layout: center of each box */

    /* Inspect view */
    int   m_igroup = 0, m_ientry = 0;
    float m_insp_yaw = 0.0f, m_insp_yaw_target = 0.0f, m_insp_pitch = 0.0f;

    /* Animation blends and effects */
    float m_mode_t = 0.0f;                /* 0 shelf .. 1 versions */
    float m_insp_t = 0.0f;                /* 0 .. 1 inspect        */
    float m_peek = 0.0f;                  /* right-stick / drag turn of the focused box */
    float m_stick_x = 0.0f, m_stick_y = 0.0f;
    float m_time = 0.0f;
    float m_info_alpha = 0.0f;
    float m_shake = 0.0f;
    float m_fade = 1.0f;
    float m_shot_scroll = 0.0f;

    /* Launch animation */
    bool  m_launching = false;
    float m_launch_t = 0.0f;
    int   m_lgroup = 0, m_lentry = 0;
    bool  m_action_sent = false;
    ShelfAction m_action;

    /* Mouse */
    bool  m_drag = false;
    float m_drag_x0 = 0.0f, m_drag_last_x = 0.0f, m_drag_peek = 0.0f;
    float m_drag_moved = 0.0f;

    /* Frame data */
    Mat4  m_view_m, m_proj_m, m_vp;
    float m_eye[3];
    std::vector<Slot> m_slots0, m_slots1;
    std::vector<Inst> m_insts;

    static Pose mix(const Pose &a, const Pose &b, float t);
    static Mat4 matrix(const Pose &p);

    int  count(void) const { return m_groups ? (int)m_groups->size() : 0; }
    bool wraps(void) const;
    const GameGroup &group(int g) const { return (*m_groups)[g]; }
    int  layers(int g) const;
    const BoxShape *shape_of(int g, int j) const;
    int  stack_layout(int g, Layer out[]) const;
    void group_extent(int g, float yaw, float &lo, float &hi) const;
    void layout_at(int F, std::vector<Slot> &out) const;
    Pose stack_pose(const Slot &s, const Layer &L, const BoxShape *sh, int layer, float turn) const;
    void layout_versions(void);
    Pose version_pose(int j, const BoxShape *sh) const;
    void build(void);
    void setup_camera(void);
    bool project(float x, float y, float z, float &sx, float &sy) const;

    void nav_group(int d);
    void nav_version(int d);
    void confirm(void);
    void back(void);
    void open_versions(void);
    void open_inspect(void);
    void close_inspect(void);
    void start_launch(int g, int j);
    void info_changed(void) { m_info_alpha = 0.0f; m_shot_scroll = 0.0f; }
    const Inst *pick(float x, float y) const;

    void draw_shelf_info(float a);
    void draw_versions_info(float a);
    void draw_inspect_info(float a);
    void draw_shots(const GameGroup &g, int j, float x, float y, float w, float h, float a);
    void draw_box_labels(float a_shelf, float a_ver);
    void draw_header(const std::string &app_name, float a_shelf, float a_ver, float a_insp);
    void draw_hints(PadStyle style);
};
