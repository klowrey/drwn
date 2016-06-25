
#pragma once

class MyRobot {
  public:
    MyRobot() {}

    virtual bool get_sensors(double * time, double* sensor) { return false; }

    virtual bool set_controls(double * u, int *pgain, int *dgain) { return false; }

    bool is_running() {return darwin_ok;}

    bool darwin_ok;

};

