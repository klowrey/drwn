//-----------------------------------//
//  This file is part of MuJoCo.     //
//  Copyright (C) 2016 Roboti LLC.   //
//-----------------------------------//

#include "mujoco.h"
#include "glfw3.h"
#include "stdlib.h"
#include "string.h"
#include <string>
#include <mutex>

#include <vector>

#include "lodepng.h"
//-------------------------------- global variables -------------------------------------

// synchronization
std::mutex gui_mutex;

// model
mjModel *m = 0;
mjData *d = 0;
mjData *d_inplace = 0;
char lastfile[1000] = "";

// user state
bool paused = true;
int increment = 0;
bool showoption = false;
bool showinfo = true;
bool showfullscreen = false;
bool slowmotion = false;
bool showdepth = false;
bool show_sensor = false;
bool save_frames = false;
int my_signal = 0;
int showhelp = 1;               // 0: none; 1: brief; 2: full
int speedtype = 1;              // 0: slow; 1: normal; 2: max

// abstract visualization
mjvScene scn;
mjvScene tmp_scn;
mjvCamera cam;
mjvOption vopt;
mjvPerturb pert;
char status[1000] = "";

// OpenGL rendering
int refreshrate;
const int fontscale = mjFONTSCALE_150;
mjrContext con;
//mjrOption ropt;
//bool stereoavailable = false;
float depth_buffer[5120 * 2880];        // big enough for 5K screen
unsigned char depth_rgb[1280 * 720 * 3];        // 1/4th of screen

// selection and perturbation
bool button_left = false;
bool button_middle = false;
bool button_right = false;
double lastx = 0;
double lasty = 0;
int needselect = 0;             // 0: none, 1: select, 2: center 
double window2buffer = 1;       // framebuffersize / windowsize (for scaled video modes)

// help strings
const char help_title[] =
"Help\n"
"Option\n"
"Info\n"
"Depth map\n"
"Stereo\n"
"Speed\n" "Pause\n" "Reset\n" "Forward\n" "Back\n" "Forward 100\n"
"Back 100\n" "Autoscale\n" "Reload\n" "Geoms\n" "Sites\n" "Select\n"
"Center\n" "Zoom\n" "Camera\n" "Perturb\n" "Switch Cam";

const char help_content[] =
"F1\n"
"F2\n"
"F3\n"
"F4\n"
"F5\n"
"Enter\n"
"Space\n"
"BackSpace\n"
"Right arrow\n"
"Left arrow\n"
"Page Down\n" "Page Up\n" "Ctrl A\n" "Ctrl L\n" "0 - 4\n" "Shift 0 - 4\n"
"L double-click\n" "R double-click\n" "Scroll or M drag\n"
"[Shift] L/R drag\n" "Ctrl [Shift] drag\n" "[ ]";

char opt_title[1000] = "";
char opt_content[1000];

GLFWwindow *window = 0;

unsigned char * im2;

//-------------------------------- utility functions ------------------------------------
void print_sensor_data(mjModel *m, mjData *d) {
  int ns= 0;
  int c = 0;
  for (int i = 0; i < m->nsensor; i++) {
    int type = m->sensor_type[i];
    switch (type) {
      case mjSENS_ACCELEROMETER:  if (c != type) printf("\nAccelerometer: "); break;        //Accelerometer
      case mjSENS_GYRO:  if (c != type) printf("\nGyro: "); break;        //Gyro
      case mjSENS_FORCE:  if (c != type) printf("\nForce: "); break;        //Force
      case mjSENS_TORQUE:  if (c != type) printf("\nTorque: "); break;        //Torque
      case mjSENS_JOINTPOS:  if (c != type) printf("\nJointPos: "); break;        //JointPos
      case mjSENS_JOINTVEL:  if (c != type) printf("\nJointVel: "); break;        //JointVel
      case mjSENS_TENDONPOS:  if (c != type) printf("\nTendonPos: "); break;        //TendonPos
      case mjSENS_TENDONVEL:  if (c != type) printf("\nTendonVel: "); break;        //TendonVel
      case mjSENS_ACTUATORPOS:  if (c != type) printf("\nActuatorPos: "); break;        //ActuatorPos
      case mjSENS_ACTUATORVEL: if (c != type) printf("\nActuatorVel: "); break;        //ActuatorVel
      case mjSENS_ACTUATORFRC: if (c != type) printf("\nActuatorFrc: "); break;        //ActuatorFrc
      case mjSENS_FRAMEPOS: if (c != type) printf("\nFramePos: "); break;        //SitePos
      case mjSENS_FRAMEQUAT: if (c != type) printf("\nFrameQuat: "); break;        //SiteQuat
      case mjSENS_MAGNETOMETER: if (c != type) printf("\nMagnetometer: "); break;        //Magnetometer (WTF?)
      default:
                                  printf("\nTODO add this unknown sensor.\n");
                                  break;
    }
    c = type;
    for (int j = ns; j < (ns + m->sensor_dim[i]); j++) {
      printf(" %4.4f ", d->sensordata[j]);
    }
    ns += m->sensor_dim[i];
  }
}

// center and scale view
void autoscale(GLFWwindow * window)
{
  // autoscale
  cam.lookat[0] = m->stat.center[0];
  cam.lookat[1] = m->stat.center[1];
  cam.lookat[2] = m->stat.center[2];
  cam.distance = 1.5 * m->stat.extent;

  // set to free camera
  cam.type = mjCAMERA_FREE;
}

// load mjb or xml model
void loadmodel(GLFWwindow * window, const char *filename,
    const char *xmlstring)
{
  // make sure one source is given
  if (!filename && !xmlstring)
    return;

  // load and compile
  char error[1000] = "could not load binary model";
  mjModel *mnew = 0;
  if (xmlstring)
    mnew = mj_loadXML(0, xmlstring, error, 1000);
  else if (strlen(filename) > 4
      && !strcmp(filename + strlen(filename) - 4, ".mjb"))
    mnew = mj_loadModel(filename, 0, 0);
  else
    mnew = mj_loadXML(filename, 0, error, 1000);
  if (!mnew) {
    printf("%s\n", error);
    return;
  }
  // delete old model, assign new
  mj_deleteData(d);
  mj_deleteModel(m);
  m = mnew;
  d = mj_makeData(m);
  mj_forward(m, d);
  d_inplace = mj_makeData(m);
  mj_forward(m, d_inplace);


  // save filename for reload
  if (!xmlstring)
    strcpy(lastfile, filename);
  else
    lastfile[0] = 0;

  // re-create custom context
  mjr_makeContext(m, &con, fontscale);

  // clear perturbation state
  pert.active = 0;
  pert.select = 0;
  needselect = 0;

  // center and scale view, update scene
  autoscale(window);
  mjv_updateScene(m, d, &vopt, &pert, &cam, mjCAT_ALL, &scn);

  // set window title to mode name
  if (window && m->names)
    glfwSetWindowTitle(window, m->names);

}

//--------------------------------- callbacks -------------------------------------------

// keyboard
void keyboard(GLFWwindow * window, int key, int scancode, int act,
    int mods)
{
  int n;

  // require model
  if (!m)
    return;

  // do not act on release
  if (act == GLFW_RELEASE)
    return;
  switch (key) {
    case GLFW_KEY_F1:          // help
      showhelp++;
      if (showhelp > 2)
        showhelp = 0;
      break;
    case GLFW_KEY_F2:          // option
      showoption = !showoption;
      break;
    case GLFW_KEY_F3:          // info
      showinfo = !showinfo;
      break;
    case GLFW_KEY_F4:          // depth
      showdepth = !showdepth;
      break;
    case GLFW_KEY_F5:          // toggle full screen
      showfullscreen = !showfullscreen;
      if (showfullscreen)
        glfwMaximizeWindow(window);
      else
        glfwRestoreWindow(window);
      break;
    case GLFW_KEY_F6:          // stereo
      scn.stereo =
        (scn.stereo ==
         mjSTEREO_NONE ? mjSTEREO_QUADBUFFERED : mjSTEREO_NONE);
      break;
    case GLFW_KEY_ENTER:       // slow motion
      slowmotion = !slowmotion;
      break;
    case GLFW_KEY_SPACE:       // pause
      paused = !paused;
      break;
    case GLFW_KEY_BACKSPACE:   // reset
      mj_resetData(m, d);
      mj_forward(m, d);
      break;
    case GLFW_KEY_RIGHT:       // step forward
      if (paused)
        increment = 1;
      break;
    case GLFW_KEY_LEFT:        // step back
      if (paused) {
        m->opt.timestep = -m->opt.timestep;
        mj_step(m, d);
        m->opt.timestep = -m->opt.timestep;
      }
      break;
    case GLFW_KEY_DOWN:        // step forward 100
      if (paused)
        increment = 100;
      break;
    case GLFW_KEY_UP:          // step back 100
      if (paused) {
        m->opt.timestep = -m->opt.timestep;
        for (n = 0; n < 100; n++)
          mj_step(m, d);
        m->opt.timestep = -m->opt.timestep;
      }
      break;

    case GLFW_KEY_F:           // next camera
      my_signal = 3;

    case GLFW_KEY_ESCAPE:      // free camera
      cam.type = mjCAMERA_FREE;
      break;
    case '[':                  // previous fixed camera or free
      if (m->ncam && cam.type == mjCAMERA_FIXED) {
        if (cam.fixedcamid > 0)
          cam.fixedcamid--;

        else
          cam.type = mjCAMERA_FREE;
      }
      break;
    case ']':                  // next fixed camera
      if (m->ncam) {
        if (cam.type != mjCAMERA_FIXED) {
          cam.type = mjCAMERA_FIXED;
          cam.fixedcamid = 0;
        }

        else if (cam.fixedcamid < m->ncam - 1)
          cam.fixedcamid++;
      }
      break;
    case ';':                  // cycle over frame rendering modes
      vopt.frame = mjMAX(0, vopt.frame - 1);
      break;
    case '\'':                 // cycle over frame rendering modes
      vopt.frame = mjMIN(mjNFRAME - 1, vopt.frame + 1);
      break;
    case '.':                  // cycle over label rendering modes
      vopt.label = mjMAX(0, vopt.label - 1);
      break;
    case '/':                  // cycle over label rendering modes
      vopt.label = mjMIN(mjNLABEL - 1, vopt.label + 1);
      break;
    default:                   // toggle flag
      // control keys
      if (mods & GLFW_MOD_CONTROL) {
        if (key == GLFW_KEY_A)
          autoscale(window);
        //else if (key == GLFW_KEY_S)
        //  show_sensor = !show_sensor;
        else if (key == GLFW_KEY_Q)
          glfwSetWindowShouldClose(window, GL_TRUE);
        else if (key == GLFW_KEY_S)
          my_signal = 1;
        else if (key == GLFW_KEY_I)
          my_signal = 2;
        else if (key == GLFW_KEY_B)
          save_frames=!save_frames;
        else if (key == GLFW_KEY_P) {
          // print state and sensors
          if (d) {
            printf("qpos(%d):\t", m->nq);
            for (int i = 0; i < m->nq; i++)
              printf("%1.2f, ", d->qpos[i]);
            printf("\nqvel(%d):\t", m->nv);
            for (int i = 0; i < m->nv; i++)
              printf("%1.2f, ", d->qvel[i]);
            int ns = 0;
            int c = 0;
            print_sensor_data(m, d);

          }
        } else if (key == GLFW_KEY_L && lastfile[0])
          loadmodel(window, lastfile, 0);
        break;
      }
      // toggle visualization flag
      for (int i = 0; i < mjNVISFLAG; i++)
        if (key == mjVISSTRING[i][2][0])
          vopt.flags[i] = !vopt.flags[i];

      // toggle rendering flag
      for (int i = 0; i < mjNRNDFLAG; i++)
        if (key == mjRNDSTRING[i][2][0])
          scn.flags[i] = !scn.flags[i];

      // toggle geom/site group
      for (int i = 0; i < mjNGROUP; i++)
        if (key == i + '0') {
          if (mods & GLFW_MOD_SHIFT)
            vopt.sitegroup[i] = !vopt.sitegroup[i];
          else
            vopt.geomgroup[i] = !vopt.geomgroup[i];
        }
  }
}

// mouse button
void mouse_button(GLFWwindow * window, int button, int act, int mods)
{
  // past data for double-click detection
  static int lastbutton = 0;
  static double lastclicktm = 0;

  // update button state
  button_left =
    (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);
  button_middle =
    (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS);
  button_right =
    (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);

  // update mouse position
  glfwGetCursorPos(window, &lastx, &lasty);

  // require model
  if (!m)
    return;

  // set perturbation
  int newperturb = 0;
  if (act == GLFW_PRESS && (mods & GLFW_MOD_CONTROL) && pert.select > 0) {
    // right: translate;  left: rotate
    if (button_right)
      newperturb = mjPERT_TRANSLATE;
    else if (button_left)
      newperturb = mjPERT_ROTATE;

    // perturbation onset: reset reference
    if (newperturb && !pert.active)
      mjv_initPerturb(m, d, &scn, &pert);
  }
  pert.active = newperturb;

  // detect double-click (250 msec)
  if (act == GLFW_PRESS && glfwGetTime() - lastclicktm < 0.25
      && button == lastbutton) {
    if (button == GLFW_MOUSE_BUTTON_LEFT)
      needselect = 1;

    else if (mods & GLFW_MOD_CONTROL)
      needselect = 3;

    else
      needselect = 2;

    // stop perturbation on select
    pert.active = 0;
  }
  // save info
  if (act == GLFW_PRESS) {
    lastbutton = button;
    lastclicktm = glfwGetTime();
  }
}

// mouse move
void mouse_move(GLFWwindow * window, double xpos, double ypos)
{
  // no buttons down: nothing to do
  if (!button_left && !button_middle && !button_right)
    return;

  // compute mouse displacement, save
  double dx = xpos - lastx;
  double dy = ypos - lasty;
  lastx = xpos;
  lasty = ypos;

  // require model
  if (!m)
    return;

  // get current window size
  int width, height;
  glfwGetWindowSize(window, &width, &height);

  // get shift key state
  bool mod_shift = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS
      || glfwGetKey(window,
        GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);

  // determine action based on mouse button
  mjtMouse action;
  if (button_right)
    action = mod_shift ? mjMOUSE_MOVE_H : mjMOUSE_MOVE_V;
  else if (button_left)
    action = mod_shift ? mjMOUSE_ROTATE_H : mjMOUSE_ROTATE_V;
  else
    action = mjMOUSE_ZOOM;

  // move perturb or camera
  if (pert.active)
    mjv_movePerturb(m, d, action, dx / height, dy / height, &scn, &pert);

  else
    mjv_moveCamera(m, action, dx / height, dy / height, &scn, &cam);
}


// scroll
void scroll(GLFWwindow * window, double xoffset, double yoffset)
{
  // require model
  if (!m)
    return;

  // scroll: emulate vertical mouse motion = 5% of window height
  mjv_moveCamera(m, mjMOUSE_ZOOM, 0, -0.05 * yoffset, &scn, &cam);
}

// drop
void drop(GLFWwindow * window, int count, const char **paths)
{
  // make sure list is non-empty
  if (count > 0)
    loadmodel(window, paths[0], 0);
}

//-------------------------------- simulation and rendering -----------------------------

// make option string
void makeoptionstring(const char *name, char key, char *buf)
{
  int i = 0, cnt = 0;

  // copy non-& characters
  while (name[i] && i < 50) {
    if (name[i] != '&')
      buf[cnt++] = name[i];
    i++;
  }

  // finish
  buf[cnt] = ' ';
  buf[cnt + 1] = '(';
  buf[cnt + 2] = key;
  buf[cnt + 3] = ')';
  buf[cnt + 4] = 0;
}


// advance simulation
void simulation(void)
{
  if (!m) {
    return;
  }
  else if (paused) {
    // apply pose perturbations, run mj_forward
    mjv_applyPerturbPose(m, d, &pert, 1);       // move mocap and dynamic bodies
    mj_step(m, d);
  }
  // running
  else {
    // slow motion factor: 10x
    /*
       mjtNum factor = (slowmotion ? 10 : 1);
    // advance effective simulation time by 1/refreshrate
    mjtNum startsimtm = d->time;
    while ((d->time - startsimtm) * factor < 1.0 / refreshrate) {

    // clear old perturbations, apply new
    mju_zero(d->xfrc_applied, 6 * m->nbody);
    if (pert.select > 0) {
    mjv_applyPerturbPose(m, d, &pert, 0);   // move mocap bodies only
    mjv_applyPerturbForce(m, d, &pert);
    }
    // run mj_step and count
    mj_step(m, d);
    }
    */
    mju_zero(d->xfrc_applied, 6 * m->nbody);
    if (pert.select > 0) {
      mjv_applyPerturbPose(m, d, &pert, 0);   // move mocap bodies only
      mjv_applyPerturbForce(m, d, &pert);
    }
    mj_step(m, d);
  }
}

// render
void render(GLFWwindow * window,
    std::vector < mjData * >frame, bool render_inplace)
{
  // past data for FPS calculation
  static double lastrendertm = 0;

  // get current framebuffer rectangle
  mjrRect rect = { 0, 0, 0, 0 };
  glfwGetFramebufferSize(window, &rect.width, &rect.height);

  double duration = 0;

  // no model: empty screen
  if (!m) {
    mjr_rectangle(rect, 0.2f, 0.3f, 0.4f, 1);
    mjr_overlay(mjFONT_NORMAL, mjGRID_TOPLEFT, rect,
        "Drag-and-drop model file here", 0, &con);

    // swap buffers
    glfwSwapBuffers(window);
    return;
  }
  // advance simulation

  //simulation();

  // start timers
  double starttm = glfwGetTime();
  //mjtNum startsimtm = d->time;

  char camstr[20];
  if (paused) {
    //TODO Uncomment this stuffto get paused control back

    // recompute to refresh rendering
    //mj_forward(m, d);

    if (increment) {
      printf("Step\n");
      for (int i = 0; i < increment; i++) {
        simulation();
      }
      increment = 0;
    }
    // 15 msec delay
    //while (glfwGetTime() - starttm < 0.015) ;
  }
  //else if (step) 
  else {
    // simulate for 15 msec of CPU time
    //int n = 0;
    simulation();
    //n++;
    // compute duration if not already computed
    //if (duration == 0 && n)
    //  duration = 1000 * (glfwGetTime() - starttm) / n;
  }

  // update simulation statistics
  if (!paused) {
    if (cam.type == mjCAMERA_FREE)
      strcpy(camstr, "Free");

    else if (cam.type == mjCAMERA_TRACKING)
      strcpy(camstr, "Tracking");

    else
      sprintf(camstr, "Fixed %d", cam.fixedcamid);
    sprintf(status,
        "%-20.1f\n%d (%d)\n%.0f\n%.2f\n%.2f (%02d it)\n%.1f %.1f\n%s\n%s\n%s",
        d->time, d->nefc, d->ncon,
        1.0 / (glfwGetTime() - lastrendertm),
        d->energy[0] + d->energy[1],
        mju_log10(mju_max
          (mjMINVAL,
           d->solver_trace[mjMAX
           (0,
            mjMIN(d->solver_iter - 1,
              mjNTRACE - 1))])),
        d->solver_iter,
        mju_log10(mju_max(mjMINVAL, d->solver_fwdinv[0])),
        mju_log10(mju_max(mjMINVAL, d->solver_fwdinv[1])), camstr,
        mjFRAMESTRING[vopt.frame], mjLABELSTRING[vopt.label]);

    if (show_sensor) {
      print_sensor_data(m, d);
    }
  }
  lastrendertm = glfwGetTime();

  if (render_inplace) {
    // set this d to be the model from estimation
    mju_copy(d_inplace->qpos, frame[0]->qpos, m->nq);
    mju_copy(d_inplace->qvel, frame[0]->qvel, m->nv);
    mj_kinematics(m, d_inplace);
    //mj_forward(m, d_inplace);
    mjv_updateScene(m, d_inplace, &vopt, &pert, &cam, mjCAT_ALL, &scn);
  } else {
    mjv_updateScene(m, d, &vopt, &pert, &cam, mjCAT_ALL, &scn);
    if (frame.size()) {
      for (unsigned int idx = 0; idx < frame.size(); idx++) {
        mj_kinematics(m, frame[idx]);
        mjv_updateScene(m, frame[idx], &vopt, NULL, &cam, mjCAT_ALL, &tmp_scn);
        mjvGeom *p = scn.geoms;
        mjvGeom *p2 = tmp_scn.geoms;
        for (int i = 0; i < tmp_scn.ngeom; i++) {
          if (idx == 0) {
            p2[i].rgba[0] = 0.0f;
            p2[i].rgba[1] = 0.0f;
            p2[i].rgba[2] = 1.0f;
            p2[i].rgba[3] = 1.0f;
          } else {
            p2[i].rgba[0] = 0.0f;
            p2[i].rgba[1] = 1.0f;
            p2[i].rgba[2] = 0.0f;
            p2[i].rgba[3] = 0.15f;
          }
          memcpy(p + (scn.ngeom + i), p2 + i, sizeof(mjvGeom));
          scn.geomorder[scn.ngeom + i] = tmp_scn.geomorder[i]; // ptrs 
        }
        scn.ngeom += tmp_scn.ngeom;
      }
    }
  }

  // selection
  if (needselect) {
    // find selected geom
    mjtNum pos[3];
    int selgeom = mjr_select(rect, &scn, &con,
        (int) (window2buffer * lastx),
        (int) (rect.height - window2buffer * lasty),
        pos, NULL);

    // find corresponding body if any
    int selbody = 0;
    if (selgeom >= 0 && selgeom < scn.ngeom
        && scn.geoms[selgeom].objtype == mjOBJ_GEOM) {
      selbody = m->geom_bodyid[scn.geoms[selgeom].objid];
      if (selbody < 0 || selbody >= m->nbody)
        selbody = 0;
    }
    // set lookat point, start tracking is requested
    if (needselect == 2 || needselect == 3) {
      if (selgeom >= 0)
        mju_copy3(cam.lookat, pos);

      // switch to tracking camera
      if (needselect == 3 && selbody) {
        cam.type = mjCAMERA_TRACKING;
        cam.trackbodyid = selbody;
        cam.fixedcamid = -1;
      }
    }
    // set body selection
    else {
      if (selbody) {
        // record selection
        pert.select = selbody;
        // compute localpos
        mjtNum tmp[3];
        mju_sub3(tmp, pos, d->xpos + 3 * pert.select);
        mju_mulMatTVec(pert.localpos, d->xmat + 9 * pert.select, tmp, 3,
            3);
      }
      else
        pert.select = 0;
    }
    needselect = 0;
  }
  // render
  mjr_render(rect, &scn, &con);

  // show depth map
  if (showdepth) {

    // get the depth buffer
    mjr_readPixels(NULL, depth_buffer, rect, &con);

    // convert to RGB, subsample by 4
    for (int r = 0; r < rect.height; r += 4)
      for (int c = 0; c < rect.width; c += 4) {

        // get subsampled address
        int adr = (r / 4) * (rect.width / 4) + c / 4;

        // assign rgb
        depth_rgb[3 * adr] = depth_rgb[3 * adr + 1] =
          depth_rgb[3 * adr + 2] =
          (unsigned char) ((1.0f - depth_buffer[r * rect.width + c]) *
              255.0f);
      }
    // show in bottom-right corner
    mjrRect bottomright = {
      (3 * rect.width) / 4, 0, rect.width / 4, rect.height / 4
    };
    mjr_drawPixels(depth_rgb, NULL, bottomright, &con);
  }
  // show overlays
  if (showhelp == 1)
    mjr_overlay(mjFONT_NORMAL, mjGRID_TOPLEFT, rect, "Help  ", "F1  ",
        &con);

  else if (showhelp == 2)
    mjr_overlay(mjFONT_NORMAL, mjGRID_TOPLEFT, rect, help_title,
        help_content, &con);

  // show info
  if (showinfo) {
    if (paused)
      mjr_overlay(mjFONT_NORMAL, mjGRID_BOTTOMLEFT, rect, "PAUSED", 0,
          &con);

    else
      mjr_overlay(mjFONT_NORMAL, mjGRID_BOTTOMLEFT, rect,
          "Time\nSize\nFPS\nEnergy\nSolver\nFwdInv\nCamera\nFrame\nLabel",
          status, &con);
  }
  // show options
  if (showoption) {
    int i;
    char buf[100];

    // fill titles on first pass
    if (!opt_title[0]) {
      for (i = 0; i < mjNRNDFLAG; i++) {
        makeoptionstring(mjRNDSTRING[i][0], mjRNDSTRING[i][2][0], buf);
        strcat(opt_title, buf);
        strcat(opt_title, "\n");
      }
      for (i = 0; i < mjNVISFLAG; i++) {
        makeoptionstring(mjVISSTRING[i][0], mjVISSTRING[i][2][0], buf);
        strcat(opt_title, buf);
        if (i < mjNVISFLAG - 1)
          strcat(opt_title, "\n");
      }
    }
    // fill content
    opt_content[0] = 0;
    for (i = 0; i < mjNRNDFLAG; i++) {
      strcat(opt_content, scn.flags[i] ? " + " : "   ");
      strcat(opt_content, "\n");
    }
    for (i = 0; i < mjNVISFLAG; i++) {
      strcat(opt_content, vopt.flags[i] ? " + " : "   ");
      if (i < mjNVISFLAG - 1)
        strcat(opt_content, "\n");
    }

    // show
    mjr_overlay(mjFONT_NORMAL, mjGRID_TOPRIGHT, rect, opt_title,
        opt_content, &con);
  }

  // swap buffers
  glfwSwapBuffers(window);

  if (save_frames) {
    std::string basename = "/tmp/frame_";
    static int nframe=0;
    int sizex, sizey;
    glfwGetWindowSize(window, &sizex, &sizey);
    //unsigned char * im=(unsigned char*)malloc(sizex*sizey*4);
    //unsigned char * imj=(unsigned char*)malloc(sizex*sizey*3);

    glPixelStorei(GL_PACK_ALIGNMENT,1);
    glReadPixels(0,0,sizex,sizey,GL_RGB,GL_UNSIGNED_BYTE,im2); 

    unsigned int error;
    unsigned char*png;
    size_t pngsize;
    error=lodepng_encode24(&png,&pngsize,im2,sizex,sizey);
    std::string filename = basename+std::to_string(nframe)+".png";
    nframe++;
    if(!error)
      lodepng_save_file(png, pngsize, filename.c_str());
    if(error)
      printf("error %u: %s\n", error, lodepng_error_text(error));

    //free(im2);
    free(png);
  }
}

void save_buffers_to_image(bool t) {save_frames = t;}

int viewer_signal()
{
  // 1 for walk
  // 2 for initialize estimator
  // 3 for f key pressed
  return my_signal;
}

void viewer_set_signal(int s)
{
  my_signal = s;
}

bool closeViewer()
{
  if (window) 
    return glfwWindowShouldClose(window);
  else
    return false;
}

void shouldCloseViewer()
{
  glfwSetWindowShouldClose(window, GL_TRUE);
}

void finalize()
{
  glfwPollEvents();
}

//-------------------------------- main function ----------------------------------------
int init_viz(std::string model_name)
{
  // print version, check compatibility
  printf("MuJoCo Pro library version %.2lf\n\n", 0.01 * mj_version());
  if (mjVERSION_HEADER != mj_version())
    mju_error("Headers and library have different versions");

  // activate MuJoCo license
  mj_activate("mjkey.txt");
  printf("MuJoCo Activated.\n");

  // init GLFW
  if (!glfwInit())
    return 1;

  // get refreshrate
  refreshrate = glfwGetVideoMode(glfwGetPrimaryMonitor())->refreshRate;

  // try stereo if refresh rate is at least 100Hz
  glfwWindowHint(GLFW_SAMPLES, 4);

  // no stereo: try mono
  if (!window) {
    glfwWindowHint(GLFW_STEREO, 0);
    window = glfwCreateWindow(1400, 900, "Simulate", NULL, NULL);
  }
  if (!window) {
    glfwTerminate();
    return 1;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  // determine retina scaling
  int width, width1, height;
  glfwGetWindowSize(window, &width, &height);
  glfwGetFramebufferSize(window, &width1, &height);
  window2buffer = (double) width1 / (double) width;

  im2=(unsigned char*)malloc(width*height*4);

  // init MuJoCo rendering
  mjv_makeScene(&scn, 10000);
  mjv_makeScene(&tmp_scn, 1000);
  mjv_defaultCamera(&cam);
  mjv_defaultOption(&vopt);
  //mjr_defaultOption(&ropt);
  mjr_defaultContext(&con);
  mjr_makeContext(m, &con, fontscale);


  // set GLFW callbacks
  glfwSetKeyCallback(window, keyboard);
  glfwSetCursorPosCallback(window, mouse_move);
  glfwSetMouseButtonCallback(window, mouse_button);
  glfwSetScrollCallback(window, scroll);
  glfwSetDropCallback(window, drop);

  //glfwSetWindowRefreshCallback(window, render);

  // load model if filename given as argument
  if (!model_name.empty())
    loadmodel(window, model_name.c_str(), 0);

  return 0;
}

void end_viz()
{
  // terminate
  mj_deleteData(d);
  mj_deleteModel(m);
  mjr_freeContext(&con);
  mjv_freeScene(&scn);

  // terminate
  glfwTerminate();
  mj_deactivate();
}
