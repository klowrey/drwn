#include "interface.h"
#include "Utilities.h"
#ifdef _WIN32
#include "WindowsDARwIn.h"
#else
#include "LinuxCM730.h"
#endif


#include <iostream>
#include <fstream>

#include <math.h>

std::ofstream myfile;
void save_states(std::string filename,
    int nu, int nsensordata, 
    double time, double * ctrl, double * sensors, 
        std::string mode = "w") {
  if (mode=="w") {
    // create file
    myfile.open(filename, std::ofstream::out);
    myfile<<"time,";
    for (int i=0; i<nu; i++) 
      myfile<<"ctrl,";
    for (int i=0; i<nsensordata; i++) 
      myfile<<"snsr,";

    myfile<<"\n";

    myfile.close();
  }
  else if (mode=="c") {
    myfile.close();
    return;
  }
  else {
    if (!myfile.is_open()) {
      printf("HAD TO OPEN OUTPUT FILE AGAIN!!!!!!\n");
      myfile.open(filename, std::ofstream::out | std::ofstream::app );
    }

    myfile<<time<<",";
    for (int i=0; i<nu; i++) 
      myfile<<ctrl[i]<<",";
    for (int i=0; i<nsensordata; i++) 
      myfile<<sensors[i]<<",";

    myfile<<"\n";
  }
}

int main (int argc, char* argv[]) {

  int nu = 20;
  bool joints = false;
  bool zero_gyro = false;
  bool use_rigid = false;
  bool use_markers = true;
  bool use_accel = false; //true;
  bool use_gyro  = false; //true;
  bool use_ati   = false;
  std::string ps_server = "128.208.4.49";

  double *p = NULL; // initial pose
  int* p_gain = new int[nu]; 
  for (int i=0; i<nu; i++) { // use sensors based on mujoco model
    p_gain[i] = 2;
  }
  DarwinRobot *d = new DarwinRobot(joints, zero_gyro, use_rigid, use_markers,
      use_accel, use_gyro, use_ati, p_gain, ps_server, p);

  if (!d->is_running()) {
    printf("\tCouldn't initialized Darwin, or some subset of its sensors!!\n");
    return 0;
  }

  // TODO get array sizes from mujoco?
  double *qpos = new double[27];
  double *qvel = new double[26];
  double *ctrl = new double[nu];
  double time = 0.0; 
  int A_SIZE=3;
  int G_SIZE=3;
  int CONTACTS_SIZE=12;
  int MARKER_SIZE = 16*4;
  int nsensordata = 40+
    use_accel*A_SIZE+
    use_gyro*G_SIZE+
    use_ati*CONTACTS_SIZE+
    use_markers*MARKER_SIZE;
  printf("Sensor Size: %d\n", nsensordata);
  double *sensors = new double[nsensordata];

  //save_states("raw.csv", nu, nsensordata, time, ctrl, sensors, "w");

  int count = 1000;
  double t1=0.0, t2=0.0;
  double * avg = new double[count];
  printf("\n");
  for (int i = 0; i < count; i++) {
    
    t1 = GetCurrentTimeMS();
    d->get_sensors(&time, sensors);
    //for (int id=0; id<20; id++) {
    //  ctrl[id] = sensors[id];
    //}
    std::this_thread::sleep_for(std::chrono::milliseconds(7));
    t2 = GetCurrentTimeMS();

    printf("%f ms\t", t2-t1);
    //for (int id=0; id<10; id++) {
    //  printf("%1.2f ", sensors[id]);
    //}
    //printf("\t::\t");
    for (int id=0; id<16; id++) {
      printf("%1.3f ", sensors[40+4*id]); // only x position
    }
    printf("\n");

    // Do stuff with data
    //save_states("raw.csv", nu, nsensordata, time, ctrl, sensors, "a");

    avg[i] = (t2-t1);
  }
  double mean=0.0;
  double stddev=0.0;
  for (int i = 0; i < count; i++) {
    mean += avg[i];
  }
  mean = mean / (double) count;
  for (int i = 0; i < count; i++) {
    double diff = avg[i] - mean;
    stddev += diff * diff;
  }
  stddev = sqrt(stddev / count);
  printf("Average Timing: %f; Standard Deviation: %f\n", mean, stddev);


  delete[] qpos;
  delete[] qvel;
  delete[] ctrl;
  delete[] sensors;
  
  //save_states("raw.csv", nu, nsensordata, 0.0, NULL, NULL, "c");

  return 0;
}

