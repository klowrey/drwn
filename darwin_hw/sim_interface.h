#include "mujoco.h"

#include "Utilities.h"

#include <string.h>
#include <random>

class SimDarwin {
  private:
    bool darwin_ok;

    mjModel *m;
    mjData *d;

    double sensor_time;
    double s_dt;

    double s_time_noise;

    std::random_device rd;
    std::mt19937 gen;
    std::normal_distribution<> t_noise;
    std::normal_distribution<> sen_noise;
    std::normal_distribution<> ctrl_noise;


    // model helpers
    int nq;
    int nv;
    int nu;

  public:
    SimDarwin(
        mjModel *m, mjData * d, double dt,
        double s_noise, double s_time_noise, double c_noise) : gen(rd()),
    t_noise(0, s_time_noise), sen_noise(0, s_noise), ctrl_noise(0, c_noise) {
      // s_noise ; magnitude of sensors noise 
      // s_time_noise ; noise is getting sensor values

      this->m = m;
      this->d = d;

      this->sensor_time = 0.0; 
      //this->s_dt = 0.0025; //ms in seconds
      //this->s_dt = 0.0055; //ms in seconds
      this->s_dt = dt; // 2x the ms of the 'robot' timestep
      this->s_time_noise = s_time_noise;

      darwin_ok = true;

      nq = m->nq;
      nv = m->nv;
      nu = m->nu;

    }

    ~SimDarwin() {
      //mj_deleteData(d);
      //mj_deleteModel(m);
      //delete[] this->i_pose;
    }

    bool is_running() {
      return darwin_ok;
    }

    //void init_pose(double *p) { // supposed to be for phasespace stuff
    //  if (!i_pose) {
    //	i_pose = new double[7];
    //  }

    //  if (p) {
    //	memcpy(i_pose, p, sizeof(double)*7);
    //  }
    //  else {
    //	memset(i_pose, 0, sizeof(double)*7);
    //  }
    //}

    //bool get_state(double* time, double* qpos, double* qvel, double *sensor) {
    bool get_sensors(double * time, double* sensor) {

      *time = d->time;
      if (d->time < sensor_time ) {
        //mj_step(m, d); // advanced simulation until we can get new sensor data
        // let render program advance the simulation
        return false;
      }

      // current time + next sensor time + sensor time noise
      sensor_time = d->time + s_dt + t_noise(gen);
      
      // get sensor values at this timestep
      mj_forward(m, d);
      mj_sensor(m, d);

      if (sensor) {
        for (int id=0; id<m->nsensordata; id++) {
          double r = sen_noise(gen);
          sensor[id] = d->sensordata[id] + r; //cs_noise perturbation;
        }
      }
      else {
        printf("Initialize sensor buffer\n");
        return false;
      }

      return true;
    }

    // mujoco controls to mujoco controls
    bool set_controls(double * u, int *pgain, int *dgain) {
      // converts controls to darwin positions
      for(int id = 0; id < nu; id++) {
        double r = ctrl_noise(gen);
        d->ctrl[id] = u[id] + r;
        //printf("%f %f %f\n", u[id], d->ctrl[id], r);
      }

      // TODO setting pgain and dgain not configured yet
      if (pgain) {
        for(int id = 0; id < nu; id++) {
          m->actuator_gainprm[id*3] = pgain[id]; // mjNGAIN = 3
          m->actuator_biasprm[(id*3)+2] = -1.0 * pgain[id]; // mjNBIAS = 3
        }
      }
      if (dgain) {
        printf("Setting d gain for position actuator in Mujoco is not supported\n");
      }

      // sets controls, should stay the same for each mjstep called in
      // get_state

      return true;
    }

};
