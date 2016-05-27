#include "mujoco.h"
#include <string.h>
#include <iostream>
#include <random>
#include <functional>

#include <omp.h>
#include <math.h>

#ifdef USE_EIGEN_MKL
#define EIGEN_USE_MKL_ALL
#endif

#define EIGEN_DONT_PARALLELIZE

//#include <Eigen/StdVector>
#include <vector>
#include <Eigen/Dense>

//#include <boost/random.hpp>
//#include <boost/random/normal_distribution.hpp>

using namespace Eigen;

class Estimator {
  public:
    Estimator(mjModel *m, mjData * d) {
      this->m = mj_copyModel(NULL, m); // make local copy of model and data
      this->d = mj_makeData(this->m);
      mj_copyData(this->d, m, d); // get current state

      this->nq = this->m->nq;
      this->nv = this->m->nv;
      this->nu = this->m->nu;
    }

    ~Estimator() {
      mj_deleteData(d);
      mj_deleteModel(m);
    }

    /*
    void get_state(mjData * d, double * state) {
      mju_copy(state, d->qpos, nq);
      mju_copy(state+nq, d->qvel, nv);
    }
    void set_state(mjData * d, double * state) {
      mju_copy(d->qpos, state, nq);
      mju_copy(d->qvel, state+nq, nv);
    }
    */
    //virtual void init();
    virtual void predict(double * ctrl, double dt) {};
    virtual void correct(double* sensors) {};

    mjModel* m;
    mjData* d;
    int nq;
    int nv;
    int nu;
};

/*
   class EKF : public Estimator {
   public:
   EKF(mjModel *m, mjData * d) : Estimator() {
   };
   ~EKF() : ~Estimator() {};

   };
   */

class UKF : public Estimator {
  public:
    UKF(mjModel *m, mjData * d,
        double _alpha, double _beta, double _kappa, double _diag, double _Ws0, double _noise,
        bool debug = false, int threads = 1) : Estimator(m, d) {
      L = nq + nv; // size of state dimensions

      ctrl_state = false;
      if (ctrl_state) L += nu;

      N = 2*L + 1;

      alpha = _alpha;
      beta = _beta;
      lambda = (alpha*alpha)*((double)L + _kappa) - (double)L;
      diag = _diag;
      Ws0 = _Ws0;
      printf("L size: %d\t N: %d\n", L, N);
      printf("Alpha %f\nBeta %f\nLambda %f\nDiag %f\nWs0 %f\n", alpha, beta, lambda, diag, Ws0);

      W_s = new double[N];
      W_c = new double[N];
      //sigma_states = new mjData*[N];
      //sigmas = new mjData*[32];
      sigma_states.resize(N);

      sigmas.resize(threads);

      stddev = mj_makeData(this->m);

      printf("Setting up %d thread models\n", threads);
      for (int i=0; i<threads; i++) {
        printf("Initialized %d model for UKF\n", i);

        sigmas[i] = mj_makeData(this->m);

        //mju_copy(sigmas[i]->qacc, this->d->qacc, nv);
        //mju_copy(sigmas[i]->qvel, this->d->qvel, nv);
        //mju_copy(sigmas[i]->qpos, this->d->qpos, nq);
        //mju_copy(sigmas[i]->ctrl, this->d->ctrl, nu);
        //mj_forward(m, sigmas[i]);

        mj_copyData(sigmas[i], this->m, this->d); // data initialization
      }

      //raw.resize(N);
      x = new VectorXd[N];
      gamma = new VectorXd[N];
      for (int i=0; i<N; i++) {
        if (i==0) {
          sigma_states[i] = this->d;
          W_s[i] = lambda / ((double) L + lambda);

          W_c[i] = W_s[i] + (1-(alpha*alpha)+beta);
          if (Ws0 < 0) {
            W_s[i] = 1.0 / N;
          }
          else{
            W_s[i] = Ws0;
          }
          //W_c[i] = W_s[i];
        }
        else {
          sigma_states[i] = mj_makeData(this->m);
          mj_copyData(sigma_states[i], this->m, this->d);

          W_c[i] = 1.0 / (2.0 * ((double)L + lambda));
          //W_c[i] = W_s[i];
          if (Ws0 < 0) {
            W_s[i] = 1.0 / N;
          }
          else{
            W_s[i] = (1.0 - Ws0) / ((double) 2 * L);
          }
        }
        x[i] = VectorXd::Zero(L); // point into raw buffer, not mjdata

        //gamma[i] = Map<VectorXd>(sigma_states[i]->sensordata, m->nsensordata);
        gamma[i] = VectorXd::Zero(m->nsensordata);

        mj_forward(m, sigma_states[i]);
      }
      double suma = 0.0;
      double sumb = 0.0;
      printf("W_s: ");
      for (int i=0; i<N; i++) {
        suma+=W_s[i];
        printf("%f ", W_s[i]); 
      }
      printf("\nW_c: ");
      for (int i=0; i<N; i++) {
        sumb+=W_c[i];
        printf("%f ", W_c[i]); 
      }
      printf("\nSums %f %f\n", suma, sumb);

      //P = new MatrixXd::Identity(L,L);
      P_t = MatrixXd::Identity(L,L) * 1e-3;
      P_z = MatrixXd::Zero(m->nsensordata,m->nsensordata);
      Pxz = MatrixXd::Zero(L,m->nsensordata);

      x_t = VectorXd::Zero(L);

      mju_copy(&x_t(0), sigma_states[0]->qpos, nq);
      mju_copy(&x_t(nq), sigma_states[0]->qvel, nv);

      this->noise = _noise;

      this->NUMBER_CHECK = debug;

      /*
         for (int i=0; i<nq; i++)
         sigma_states[0]->qpos[i] = (double) i;
         for (int i=0; i<nv; i++)
         this->d->qvel[i] = (double) i+nq;

         mju_copy(&x[0](0), this->d->qpos, nq);
         mju_copy(&x[0](nq), sigma_states[0]->qvel, nv);

         printf("\n HELOOOOOOOOOOOOOOOOOO\n");
         for (int i=0; i<L; i++)
         printf("%f ", x[0](i));
         printf("\n\n");
         */

      //x_t = Map<VectorXd> (p_state, L);
      //x_t.setZero();
    };

    ~UKF() {
      delete[] x;
      delete[] W_s;
      delete[] W_c;
      //delete[] p_state;
      delete[] gamma;
      delete[] W_s;
      delete[] W_c;
    };

    double random_sample(double a) {
      static std::random_device rd;
      static std::mt19937 rng(rd());
      static std::normal_distribution<> nd(0, 0.01);

      return nd(rng);
    }

    double sensor_sample(double a) {
      static std::random_device rd;
      static std::mt19937 rng(rd());
      static std::normal_distribution<> nd(0, 0.01);

      return nd(rng);
    }

    void set_data(mjData* data, VectorXd x) {
      mju_copy(data->qpos,   &x(0), nq);
      mju_copy(data->qvel,   &x(nq), nv);
      if (ctrl_state) mju_copy(data->ctrl,   &x(nq+nv), nu); // set controls for this t
    }

    void get_state(mjData* data, VectorXd x) {
      mju_copy(&x(0),  data->qpos, nq);
      mju_copy(&x(nq), data->qvel, nv);
      if (ctrl_state) mju_copy(&x(nq+nv), data->ctrl, nu);
    }

    void copy_state(mjData * dst, mjData * src) {
      mju_copy(dst->qpos, src->qpos, nq);
      mju_copy(dst->qvel, src->qvel, nv);
      if (ctrl_state) mju_copy(dst->ctrl, src->ctrl, nu);
    }

    void fast_forward(mjData * t_d, int index) {
      //if (ctrl_state && index >= (nq+nv)) mj_forwardSkip(m, t_d, 2); // just ctrl calcs
      //else if (index >= nq) mj_forwardSkip(m, t_d, 1); // velocity cals
      //else mj_forward(m, t_d); // all calculations
      mj_forward(m, t_d); // all calculations
    }

    void predict_correct(double * ctrl, double dt, double* sensors) {

      double t2 = omp_get_wtime()*1000.0;

      set_data(d, x_t);
      //mju_copy(d->qpos, &(x_t(0)), nq); // previous estimate
      //mju_copy(d->qvel, &(x_t(nq)), nv);
      //if (ctrl_state) mju_copy(&x_t(nq+nv), ctrl, nu); // TODO this line??
      mju_copy(d->ctrl, ctrl, nu); // set controls for the center point

      x[0] = x_t;

      double t3 = omp_get_wtime()*1000.0;

      LLT<MatrixXd> chol((L+lambda)*(P_t));
      MatrixXd sqrt = chol.matrixL(); // chol

      double t4 = omp_get_wtime()*1000.0;

      // Simulation options
      m->opt.timestep = dt;
      m->opt.iterations = 100; 
      m->opt.tolerance = 1e-6; 

      mj_forward(m, d); // solve for center point accurately

      m->opt.iterations = 5; 
      m->opt.tolerance = 0; 

      // set tolerance to be low, run 50, 100 iterations for mujoco solver
      // copy qacc for sigma points with some higher tolerance
#pragma omp parallel
      {
        //double omp1 = omp_get_wtime()*1000.0;
        int tid = omp_get_thread_num();
        int t = omp_get_num_threads();
        int s = tid * L / t;
        int e = (tid + 1 ) * L / t;
        if (tid == t-1) e = L;

        mjData *t_d = sigmas[tid];

        //printf("p thread: %d chunk: %d-%d \n", tid, s, e);
        for (int j=s; j<e; j++) {
          // step through all the perturbation cols and collect the data
          int i = j+1;

          x[i+0] = x[0]+sqrt.col(i-1);
          x[i+L] = x[0]-sqrt.col(i-1);

          // sigma point
          set_data(t_d, x[i+0]);
          mju_copy(t_d->qacc, d->qacc, nv); // copy from center point
          if (!ctrl_state) mju_copy(t_d->ctrl, ctrl, nu); // set controls for this t

          //mj_step(m, t_d);
          fast_forward(t_d, j);
          mj_Euler(m, t_d);

          get_state(t_d, x[i+0]);

          //mj_forward(m, t_d); // at new position
          fast_forward(t_d, j);

          mj_sensor(m, t_d);
          gamma[i] = Map<VectorXd>(t_d->sensordata, m->nsensordata);

          mju_copy(sigma_states[i+0]->qpos, t_d->qpos, nq);
          mju_copy(sigma_states[i+0]->qvel, t_d->qvel, nv);

          ////////////////////// symmetric point
          t_d->time = d->time;
          set_data(t_d, x[i+L]);
          mju_copy(t_d->qacc, d->qacc, nv); // copy from center point

          //mj_step(m, t_d);
          //mj_forward(m, t_d);
          fast_forward(t_d, j);

          mj_Euler(m, t_d);

          get_state(t_d, x[i+L]);

          //mj_forward(m, t_d); // at new position

          fast_forward(t_d, j);
          mj_sensor(m, t_d);
          gamma[i+L] = Map<VectorXd>(t_d->sensordata, m->nsensordata);

          copy_state(sigma_states[i+L], t_d); // for visualizations
        }
        //double omp2 = omp_get_wtime()*1000.0;
        //printf("p thread: %d chunk: %d-%d Time: %f\n", tid, s, e, omp2-omp1);
      }

      // step for the central point
      //mj_forward(m, d);
      mj_Euler(m, d); // step

      get_state(d, x[0]);
      //mju_copy(&x[0](0), d->qpos, nq);
      //mju_copy(&x[0](nq), d->qvel, nv);
      //if (ctrl_state) mju_copy(&x[0](nq+nv), d->ctrl, nu);

      mj_forward(m, d);
      mj_sensor(m, d); // sensor values at new positions
      gamma[0] = Map<VectorXd>(d->sensordata, m->nsensordata);

      copy_state(sigma_states[0], d); // for visualizations
      mju_copy(sigma_states[0]->ctrl, d->ctrl, nu);

      double t5 = omp_get_wtime()*1000.0;

      x_t.setZero();
      for (int i=0; i<N; i++) {
        x_t += W_s[i]*x[i];
      }

      VectorXd z_k = VectorXd::Zero(m->nsensordata);
      for (int i=0; i<N; i++) {
        z_k += W_s[i]*gamma[i];
      }

      if (NUMBER_CHECK) {
        IOFormat CleanFmt(3, 0, ", ", "\n", "[", "]");
        printf("Correct: After Step time %f\n", sigma_states[0]->time);
        for (int i=0; i<N; i++) {
          printf("%d\t", i);
          for (int j=0; j<nq; j++)
            printf("%1.5f ", sigma_states[i]->qpos[j]);
          for (int j=0; j<nv; j++)
            printf("%1.5f ", sigma_states[i]->qvel[j]);
          printf("\t:: ");
          for (int j=0; j<nu; j++)
            printf("%1.5f ", sigma_states[i]->ctrl[j]);
          printf("\t:: ");
          for (int j=0; j<m->nsensordata; j++)
            printf("%f ", sigma_states[i]->sensordata[j]);
          printf("\n");
        }
        std::cout << "\nz_k hat:\n"<< (z_k).transpose().format(CleanFmt) << std::endl;
      }

      P_t.setZero();
      P_z.setZero();
      //P_z.setIdentity();
      Pxz.setZero();
      for (int i=0; i<N; i++) {
        VectorXd z(gamma[i] - z_k);
        VectorXd x_i(x[i] - x_t);

        P_t += W_c[i] * (x_i * x_i.transpose());
        P_z += W_c[i] * (z * z.transpose());
        Pxz += W_c[i] * (x_i * z.transpose());
      }

      for (int i=0; i<L; i++) {
        if (P_t.row(i).isZero(1e-9)) {
          P_t.row(i).setZero();
          P_t.col(i).setZero();
          P_t(i,i) = 1e-1; // TODO diag??
        }
      }

      if (NUMBER_CHECK) {
        IOFormat CleanFmt(3, 0, ", ", "\n", "[", "]");
        std::cout << "Prediction output:\nx_t\n"<< x_t.transpose().format(CleanFmt) << std::endl;
        std::cout << "Prediction output:\nP_t\n"<< P_t.format(CleanFmt) << std::endl;
      }

      // TODO make the identity addition a parameter
      MatrixXd PzAdd = MatrixXd::Identity(m->nsensordata, m->nsensordata);
      for (int i = 0; i < m->nsensordata; i++) {
        int type = m->sensor_type[i];
        if(type == 1) {    //Accelerometer
          PzAdd(i, i) = 1e-5;
        }else if(type == 2) {    //Gyro
          PzAdd(i, i) = 1e-5;
        }else if(type == 3) {    //Force
          PzAdd(i, i) = 1e-2;
        }else if(type == 4) {    //Torque
          PzAdd(i, i) = 1e-2;
        }else if(type == 5) {    //JointPos
          PzAdd(i, i) = 1e-4;
        }else if(type == 6) {    //JointVel
          PzAdd(i, i) = 1e-5;
        }else if(type == 7) {    //TendonPos
          PzAdd(i, i) = 1e-4;
        }else if(type == 8) {    //TendonVel
          PzAdd(i, i) = 1e-5;
        }else if(type == 9) {    //ActuatorPos
          PzAdd(i, i) = 1e-4;
        }else if(type == 10) {    //ActuatorVel
          PzAdd(i, i) = 1e-5;
        }else if(type == 11) {    //ActuatorFrc
          PzAdd(i, i) = 1e-3;
        }else if(type == 12) {    //SitePos
          PzAdd(i, i) = 1e-4;
        }else if(type == 13) {    //SiteQuat
          PzAdd(i, i) = 1e-4;
        }else { //(type == 14) {    //Magnetometer (WTF?)
          PzAdd(i, i) = 1e-5;
        }
      }
      
      P_z = P_z + PzAdd;

      MatrixXd K = Pxz * P_z.inverse();

      VectorXd s = Map<VectorXd>(sensors, m->nsensordata); // map our real z to vector
      x_t = x_t + (K*(s-z_k));
      P_t = P_t - (K * P_z * K.transpose());

      //for (int i=0; i<L; i++) {
      //  if (P_t.row(i).isZero(1e-9)) {
      //    P_t.row(i).setZero();
      //    P_t.col(i).setZero();
      //    P_t(i,i) = 1.0;
      //  }
      //}

      //mju_copy(d->qpos, &(x_t(0)), nq);
      //mju_copy(d->qvel, &(x_t(nq)), nv);

      double t6 = omp_get_wtime()*1000.0;

      printf("\ncombo init %f, sqrt %f, mjsteps %f, merge %f\n",
          t3-t2, t4-t3, t5-t4, t6-t5);

      if (NUMBER_CHECK) {
        IOFormat CleanFmt(3, 0, ", ", "\n", "[", "]");
        std::cout << "Kalman Gain:\n"<< K.format(CleanFmt) << std::endl;
        std::cout << "\nPz  :\n"<< P_z.format(CleanFmt) << std::endl;
        std::cout << "\nPz^-1:\n"<< P_z.inverse().format(CleanFmt) << std::endl;
        std::cout << "\nPxz :\n"<< Pxz.format(CleanFmt) << std::endl;
        std::cout << "\ncorrect p_t :\n"<< P_t.format(CleanFmt) << std::endl;
        std::cout << "\ncorrect x_t:\n"<< x_t.transpose().format(CleanFmt) << std::endl;
        std::cout << "\nraw sensors:\n"<< (s).transpose().format(CleanFmt) << std::endl;
        std::cout << "\nz_k hat:\n"<< (z_k).transpose().format(CleanFmt) << std::endl;
        std::cout << "\ndelta:\n"<< (s-z_k).transpose().format(CleanFmt) << std::endl;
        std::cout << "Predict and Correct COMBO\n";
      }
    }

    void predict(double * ctrl, double dt) {

      IOFormat CleanFmt(3, 0, ", ", "\n", "[", "]");

      double t2 = omp_get_wtime()*1000.0;

      m->opt.timestep = dt; // smoother way of doing this?

      mju_copy(d->ctrl, ctrl, nu); // set controls for the center point
      if (ctrl_state) mju_copy(&x_t(nq+nv), ctrl, nu);
      // qpos and qvel set previously

      // d has the previous estimate already set
      //mju_copy(&x[0](0),  d->qpos, nq);
      //mju_copy(&x[0](nq), d->qvel, nv);
      x[0] = x_t;

      double t3 = omp_get_wtime()*1000.0;

      //P_t = MatrixXd::Identity(L,L) * 1e-3;
      LLT<MatrixXd> chol((L+lambda)*(P_t));
      MatrixXd sqrt = chol.matrixL(); // chol

      double t4 = omp_get_wtime()*1000.0;

      if (NUMBER_CHECK) {
        std::cout<<"\n\nPrevious Prediction x_t:\n"<<x_t.transpose().format(CleanFmt)<<"\n";
        std::cout<<"\n\nPrediction P_T:\n"<<P_t.format(CleanFmt)<<"\n\n";
        printf("Before Step:\n");
        for (int i=1; i<=L; i++) {
          printf("%d ", i);
          std::cout<<(x[i+0]+sqrt.col(i-1)).transpose().format(CleanFmt)<<"\n";
          std::cout<<(x[i+L]+sqrt.col(i-1)).transpose().format(CleanFmt);
          printf("\n");
        }
      }


#pragma omp parallel
      {
        volatile int tid = omp_get_thread_num();
        int t = omp_get_num_threads();
        int chunk = (L + t-1) / t;
        int s = tid * chunk;
        int e = mjMIN(s+chunk, L);

        //printf("p thread: %d chunk: %d-%d \n", tid, s, e);
        for (int j=s; j<e; j++) {
          // step through all the perturbation cols and collect the data
          int i = j+1;

          x[i+0] = x[0]+sqrt.col(i-1);
          x[i+L] = x[0]-sqrt.col(i-1);

          mju_copy(sigmas[tid]->qpos,   &x[i](0), nq);
          mju_copy(sigmas[tid]->qvel,   &x[i](nq), nv);
          //mju_copy(sigmas[tid]->qacc,   d->qacc, nv);
          if (ctrl_state) mju_copy(sigmas[tid]->ctrl,   &x[i](nq+nv), nu); // set controls for this t
          else mju_copy(sigmas[tid]->ctrl,   ctrl, nu); // set controls for this t

          //mj_step(m, sigmas[tid]);
          mj_forward(m, sigmas[tid]);
          mj_Euler(m, sigmas[tid]);

          sigmas[tid]->time = d->time;
          mju_copy(&x[i](0),  sigmas[tid]->qpos, nq);
          mju_copy(&x[i](nq), sigmas[tid]->qvel, nv);
          if (ctrl_state) mju_copy(&x[i](nq+nv), sigmas[tid]->ctrl, nu);

          if (NUMBER_CHECK) mju_copy(sigma_states[i]->qpos, sigmas[tid]->qpos, nq);

          mju_copy(sigmas[tid]->qpos,   &x[i+L](0), nq);
          mju_copy(sigmas[tid]->qvel,   &x[i+L](nq), nv);
          //mju_copy(sigmas[tid]->qacc,   d->qacc, nv);
          if (ctrl_state) mju_copy(sigmas[tid]->ctrl,   &x[i+L](nq+nv), nu); // set controls for this t
          else mju_copy(sigmas[tid]->ctrl,   ctrl, nu); // set controls for this t

          //mj_step(m, sigmas[tid]);
          mj_forward(m, sigmas[tid]);
          mj_Euler(m, sigmas[tid]);

          mju_copy(&x[i+L](0),  sigmas[tid]->qpos, nq);
          mju_copy(&x[i+L](nq), sigmas[tid]->qvel, nv);
          if (ctrl_state) mju_copy(&x[i+L](nq+nv), sigmas[tid]->ctrl, nu);

          if (NUMBER_CHECK) mju_copy(sigma_states[i+L]->qpos, sigmas[tid]->qpos, nq);
        }
      }

      // step for the central point
      mj_step(m, d);
      mju_copy(&x[0](0), d->qpos, nq);
      mju_copy(&x[0](nq), d->qvel, nv);
      if (ctrl_state) mju_copy(&x[0](nq+nv), d->ctrl, nu);

      if (NUMBER_CHECK) mju_copy(sigma_states[0]->qpos, d->qpos, nq);

      if (NUMBER_CHECK) {
        printf("Predict t-0 = %f seconds\n", sigma_states[0]->time);
        printf("After Step:\n");
        for (int i=0; i<N; i++) {
          printf("%d ", i);
          std::cout<<x[i].transpose().format(CleanFmt);
          printf("\t:: ");
          for (int j=0; j<nu; j++)
            printf("%1.5f ", sigma_states[i]->ctrl[j]);
          printf("\t:: ");
          for (int j=0; j<m->nsensordata; j++)
            printf("%f ", sigma_states[i]->sensordata[j]);

          printf("\n");
        }
      }

      double t5 = omp_get_wtime()*1000.0;

      x_t.setZero();
      for (int i=0; i<N; i++) {
        if (NUMBER_CHECK) {
          std::cout<<W_s[i]<<"  "<<(W_s[i]*x[i]).transpose().format(CleanFmt)<<"\n";
        }
        x_t += W_s[i]*x[i];
      }
      //x_t = (W_s[0] * x[0]) + ((N-1)*W_s[1] * x_t);

      P_t.setZero();
      for (int i=0; i<N; i++) {
        VectorXd x_i(x[i] - x_t);
        P_t += W_c[i] * (x_i * x_i.transpose());
      }

      for (int i=0; i<L; i++) {
        if (P_t.row(i).isZero(1e-9)) {
          P_t.row(i).setZero();
          P_t.col(i).setZero();
          P_t(i,i) = 1.0;
        }
      }

      //mju_copy(d->qpos, &(x_t(0)), nq); // center point
      //mju_copy(d->qvel, &(x_t(nq)), nv);
      mju_copy(d->qpos, &(x_t(0)), nq);
      mju_copy(d->qvel, &(x_t(nq)), nv);

      double t6 = omp_get_wtime()*1000.0;

      printf("\npredict init %f, sqrt %f, mjsteps %f, merge %f\n",
          t3-t2, t4-t3, t5-t4, t6-t5);

      if (NUMBER_CHECK) std::cout << "Prediction output:\nx_t\n"<< x_t.transpose().format(CleanFmt) << std::endl;
      if (NUMBER_CHECK) std::cout << "Prediction output:\nP_t\n"<< P_t.format(CleanFmt) << std::endl;

    }

    void correct(double* sensors) {

      IOFormat CleanFmt(4, 0, ", ", "\n", "[", "]");
      double t2 = omp_get_wtime()*1000.0;

      mju_copy(d->qpos, &(x_t(0)), nq);
      mju_copy(d->qvel, &(x_t(nq)), nv);
      if (ctrl_state) mju_copy(d->ctrl, &(x_t(nq+nv)), nu);

      double t3 = omp_get_wtime()*1000.0;

      //P_t = MatrixXd::Identity(L,L) * 1e-3;
      LLT<MatrixXd> chol((L+lambda)*(P_t));
      MatrixXd sqrt = chol.matrixL(); // chol

      double t4 = omp_get_wtime()*1000.0;

      // TODO set solver iterations and tolerance
      mju_copy(sigma_states[0]->qpos, d->qpos, nq);
      mju_copy(sigma_states[0]->qvel, d->qvel, nv);
      mju_copy(sigma_states[0]->ctrl, d->ctrl, nu);

      mj_forward(m, d);
      mj_sensor(m, d);
      //mju_copy(&x[0](0), d->qpos, nq);
      //mju_copy(&x[0](nq), d->qvel, nv);

      x[0] = x_t;
      gamma[0] = Map<VectorXd>(d->sensordata, m->nsensordata);

      if (NUMBER_CHECK) std::cout << "Correction sqrt:\n"<< sqrt.format(CleanFmt) << std::endl;
#pragma omp parallel
      {
        int tid = omp_get_thread_num();
        int t = omp_get_num_threads();
        int chunk = (L + t-1) / t;
        int s = tid * chunk;
        int e = mjMIN(s+chunk, L);

        //printf("c thread: %d chunk: %d-%d \n", tid, s, e);
        for (int j=s; j<e; j++) {
          int i = j+1;

          x[i+0] = x[0]+sqrt.col(i-1);
          x[i+L] = x[0]-sqrt.col(i-1);

          mju_copy(sigmas[tid]->qpos, &x[i](0), nq);
          mju_copy(sigmas[tid]->qvel, &x[i](nq), nv);
          //mju_copy(sigmas[tid]->qacc, d->qacc, nv);
          if (ctrl_state) mju_copy(sigmas[tid]->ctrl, &x[i](nq+nv), nu); // set controls for this t

          // TODO set solver iterations and tolerance
          mj_forward(m, sigmas[tid]);
          mj_sensor(m, sigmas[tid]);

          gamma[i] = Map<VectorXd>(sigmas[tid]->sensordata, m->nsensordata);

          mju_copy(sigma_states[i+0]->qpos, sigmas[tid]->qpos, nq);
          mju_copy(sigma_states[i+0]->qvel, sigmas[tid]->qvel, nv);
          if (ctrl_state) mju_copy(sigma_states[i+0]->ctrl, sigmas[tid]->ctrl, nu);

          //sigmas[tid]->time = d->time;
          mju_copy(sigmas[tid]->qpos, &x[i+L](0), nq);
          mju_copy(sigmas[tid]->qvel, &x[i+L](nq), nv);
          //mju_copy(sigmas[tid]->qacc, d->qacc, nv);
          //if (!ctrl_state) mju_copy(sigmas[tid]->ctrl,   ctrl, nu); // set controls for this t
          if (ctrl_state) mju_copy(sigmas[tid]->ctrl, &x[i+L](nq+nv), nu); // set controls for this t

          mj_forward(m, sigmas[tid]);
          mj_sensor(m, sigmas[tid]);

          gamma[i+L] = Map<VectorXd>(sigmas[tid]->sensordata, m->nsensordata);

          mju_copy(sigma_states[i+L]->qpos, sigmas[tid]->qpos, nq);
          mju_copy(sigma_states[i+L]->qvel, sigmas[tid]->qvel, nv);
          //mju_copy(sigma_states[i+L]->ctrl, sigmas[tid]->ctrl, nu);
        }
      }

      double t5 = omp_get_wtime()*1000.0;

      VectorXd z_k = VectorXd::Zero(m->nsensordata);
      for (int i=0; i<N; i++) {
        z_k += W_s[i]*gamma[i];
      }

      if (NUMBER_CHECK) {
        printf("Correct: After Step time %f\n", sigma_states[0]->time);
        for (int i=0; i<N; i++) {
          printf("%d\t", i);
          for (int j=0; j<nq; j++)
            printf("%1.5f ", sigma_states[i]->qpos[j]);
          for (int j=0; j<nv; j++)
            printf("%1.5f ", sigma_states[i]->qvel[j]);
          printf("\t:: ");
          for (int j=0; j<nu; j++)
            printf("%1.5f ", sigma_states[i]->ctrl[j]);
          printf("\t:: ");
          for (int j=0; j<m->nsensordata; j++)
            printf("%f ", sigma_states[i]->sensordata[j]);
          printf("\n");
        }
        std::cout << "\nz_k hat:\n"<< (z_k).transpose().format(CleanFmt) << std::endl;
      }

      P_z.setZero();
      //P_z.setIdentity();
      Pxz.setZero();
      for (int i=0; i<N; i++) {
        VectorXd z(gamma[i] - z_k);
        VectorXd x_i(x[i] - x_t);

        P_z += W_c[i] * (z * z.transpose());
        Pxz += W_c[i] * (x_i * z.transpose());
      }

      // TODO make the identity addition a parameter
      P_z = P_z + (MatrixXd::Identity(m->nsensordata,m->nsensordata)*diag);

      //P_z.setIdentity();
      //P_t = MatrixXd::Identity(L,L) * 1e-3;
      MatrixXd K = Pxz * P_z.inverse();

      VectorXd s = Map<VectorXd>(sensors, m->nsensordata); // map our real z to vector
      //std::cout << "\nbefore x_t:\n"<< x_t.transpose().format(CleanFmt) << std::endl;
      x_t = x_t + (K*(s-z_k));
      P_t = P_t - (K * P_z * K.transpose());

      //std::cout << "\nafter x_t:\n"<< x_t.transpose().format(CleanFmt) << std::endl;

      mju_copy(d->qpos, &(x_t(0)), nq); // center point
      mju_copy(d->qvel, &(x_t(nq)), nv);

      double t6 = omp_get_wtime()*1000.0;

      printf("\n\ncorrect copy %f, sqrt %f, mjsteps %f, merge %f\n",
          t3-t2, t4-t3, t5-t4, t6-t5);

      if (NUMBER_CHECK) {
        std::cout << "Kalman Gain:\n"<< K.format(CleanFmt) << std::endl;
        std::cout << "\nPz  :\n"<< P_z.format(CleanFmt) << std::endl;
        std::cout << "\nPz^-1:\n"<< P_z.inverse().format(CleanFmt) << std::endl;
        std::cout << "\nPxz :\n"<< Pxz.format(CleanFmt) << std::endl;
        std::cout << "\ncorrect p_t :\n"<< P_t.format(CleanFmt) << std::endl;
        std::cout << "\ncorrect x_t:\n"<< x_t.transpose().format(CleanFmt) << std::endl;
        std::cout << "\nraw sensors:\n"<< (s).transpose().format(CleanFmt) << std::endl;
        std::cout << "\nz_k hat:\n"<< (z_k).transpose().format(CleanFmt) << std::endl;
        std::cout << "\ndelta:\n"<< (s-z_k).transpose().format(CleanFmt) << std::endl;
        std::cout << "NEW CORRECTION STEPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPP\n";
      }

      /* Entropy Testing?
         std::cout << "\ndelta:\n"<< (s-z_k).transpose().format(CleanFmt) << std::endl;
         std::cout << "\ncorrection:\n"<< (K*(s-z_k)).transpose().format(CleanFmt) << std::endl;
         std::cout << "\np_t :\n"<< P_t.diagonal().transpose().format(CleanFmt) << std::endl;
         double a = abs(P_t.determinant());
         double b = pow(2*3.14159265*exp(1), (double)L);
         double entropy = 0.5*log(b * a);
         printf("\n\ne %f\ta: %1.32f\tb: %f\tEntropy: %f\n\n", a*b, a, b, entropy);
         */
    }

    void old_predict(double * ctrl, double dt) {

      m->opt.timestep = dt; // smoother way of doing this?

      IOFormat CleanFmt(3, 0, ", ", "\n", "[", "]");

      printf("Predict t-1 = %f seconds\n", sigma_states[0]->time);

      double t1 = omp_get_wtime()*1000.0;

      mju_copy(d->ctrl, ctrl, nu); // set controls for this t

      // copy to our data vectors
      mju_copy(&x[0](0), d->qpos, nq);
      mju_copy(&x[0](nq), d->qvel, nv);

      double t2 = omp_get_wtime()*1000.0;

      // get the matrix square root
      if (NUMBER_CHECK) {
        std::cout<<"\n\nPrediction P_T:\n"<<P_t.format(CleanFmt)<<"\n\n";
      }
      LLT<MatrixXd> chol((L+lambda)*(P_t));
      MatrixXd sqrt = chol.matrixL(); // chol
      //std::cout<<"\n\nPrediction sqrt(P_T):\n"<<sqrt.diagonal().transpose().format(CleanFmt)<<"\n\n";

      double t3 = omp_get_wtime()*1000.0;

      // perturb x with covariance values => make sigma point vectors
#pragma omp parallel for schedule(static, 10)
      for (int i=1; i<=L; i++) {
        //x[i+0] += sqrt.col(i-1);
        //x[i+L] -= sqrt.col(i-1);
        x[i+0] = x[0]+sqrt.col(i-1);
        x[i+L] = x[0]-sqrt.col(i-1);
        //x[i+0] = x[0];
        //x[i+L] = x[0];

        mju_copy(sigma_states[i]->qpos,   &x[i](0), nq);
        mju_copy(sigma_states[i]->qvel,   &x[i](nq), nv);
        mju_copy(sigma_states[i]->ctrl,   ctrl, nu); // set controls for this t
        //mju_copy(sigma_states[i]->qacc, d->qacc, nv);

        mj_step(m, sigma_states[i]);

        mju_copy(&x[i](0),  sigma_states[i]->qpos, nq);
        mju_copy(&x[i](nq), sigma_states[i]->qvel, nv);

        mju_copy(sigma_states[i+L]->qpos, &x[i+L](0), nq);
        mju_copy(sigma_states[i+L]->qvel, &x[i+L](nq), nv);
        mju_copy(sigma_states[i+L]->ctrl, ctrl, nu); // set controls for this t
        //mju_copy(sigma_states[i+L]->qacc, d->qacc, nv);

        mj_step(m, sigma_states[i+L]);

        mju_copy(&x[i+L](0),  sigma_states[i+L]->qpos, nq);
        mju_copy(&x[i+L](nq), sigma_states[i+L]->qvel, nv);
      }
      mj_step(m, sigma_states[0]);
      mju_copy(&x[0](0),  sigma_states[0]->qpos, nq);
      mju_copy(&x[0](nq), sigma_states[0]->qvel, nv);

      double t4 = omp_get_wtime()*1000.0;

      if (NUMBER_CHECK) {
        std::cout << "Prediction sqrt(P_T):\n"<< sqrt.format(CleanFmt) << std::endl;
        printf("Before Step:\n");
        for (int i=0; i<N; i++) {
          printf("%d ", i);
          for (int j=0; j<nq; j++)
            printf("%1.5f ", sigma_states[i]->qpos[j]);
          for (int j=0; j<nv; j++)
            printf("%1.5f ", sigma_states[i]->qvel[j]);
          printf("\t:: ");
          for (int j=0; j<nu; j++)
            printf("%1.5f ", sigma_states[i]->ctrl[j]);
          printf("\t:: ");
          for (int j=0; j<m->nsensordata; j++)
            printf("%f ", sigma_states[i]->sensordata[j]);
          printf("\n");
        }
      }

      /*
#pragma omp parallel for schedule(static, 10)
for (int i=0; i<N; i++) {
mj_step(m, sigma_states[i]);
      //mj_Euler(m, sigma_states[i]); // WOW, that's fast

      mju_copy(&x[i](0), sigma_states[i]->qpos, nq);
      mju_copy(&x[i](nq), sigma_states[i]->qvel, nv);

      // TODO can put the W_s multiplication here
      }
      */

      if (NUMBER_CHECK) {
        printf("Predict t-0 = %f seconds\n", sigma_states[0]->time);
        printf("After Step:\n");
        for (int i=0; i<N; i++) {
          printf("%d ", i);
          for (int j=0; j<nq; j++)
            printf("%1.5f ", sigma_states[i]->qpos[j]);
          for (int j=0; j<nv; j++)
            printf("%1.5f ", sigma_states[i]->qvel[j]);
          printf("\t:: ");
          for (int j=0; j<nu; j++)
            printf("%1.5f ", sigma_states[i]->ctrl[j]);
          printf("\t:: ");
          for (int j=0; j<m->nsensordata; j++)
            printf("%f ", sigma_states[i]->sensordata[j]);

          printf("\n");
        }
      }

      double t5 = omp_get_wtime()*1000.0;

      x_t.setZero();
      //std::cout << "\n0pred: "<< x_t.transpose() << std::endl;
      for (int i=0; i<N; i++) {
        x_t += W_s[i]*x[i];
      }
      //x_t = (W_s[0] * x[0]) + ((N-1)*W_s[1] * x_t);

      std::cout << "\n1pred: "<< x_t.transpose() << std::endl;

      //P_t = P_t + 0.0001;
      //P_t.setIdentity() * 0.0001;
      // TODO random noise in the covariance, with symmetry
      //P_t.setIdentity();
      P_t.setZero();
      for (int i=0; i<N; i++) {
        VectorXd x_i(x[i] - x_t);
        P_t += W_c[i] * (x_i * x_i.transpose());
      }
      //std::cout<<"\n\nPrediction P_T:\n"<<P_t.diagonal().transpose().format(CleanFmt)<<"\n\n";
      for (int i=0; i<L; i++) {
        if (P_t.row(i).isZero(1e-9)) {
          P_t.row(i).setZero();
          P_t.col(i).setZero();
          P_t(i,i) = 1.0;
        }
      }
      //std::cout<<"\n\nFixed?? P_T:\n"<<P_t.format(CleanFmt)<<"\n\n";

      double t6 = omp_get_wtime()*1000.0;

      if (NUMBER_CHECK) {
        std::cout << "Prediction output:\n"<< P_t.format(CleanFmt) << std::endl;
      }

      printf("\npredict copy %f, sqrt %f, sigmas %f, mjsteps %f, merge %f\n",
          t2-t1, t3-t2, t4-t3, t5-t4, t6-t5);

      // x_t, P_t are outputs
      if (NUMBER_CHECK) std::cout << "Prediction output:\nx_t\n"<< x_t.transpose().format(CleanFmt) << std::endl;
      if (NUMBER_CHECK) std::cout << "Prediction output:\nP_t\n"<< P_t.format(CleanFmt) << std::endl;
    }

    //////////////////////////////////////////////////////////////////////////////////////////
    void old_correct(double* sensors) {

      IOFormat CleanFmt(4, 0, ", ", "\n", "[", "]");

      // todo 
      mju_copy(d->qpos, &(x_t(0)), nq);
      mju_copy(d->qvel, &(x_t(nq)), nv);

      //mj_forward(m, d);

      mju_copy(&x[0](0), d->qpos, nq);
      mju_copy(&x[0](nq), d->qvel, nv);

      // get state of point 0
      double t1 = omp_get_wtime()*1000.0;

      // copy used to go here

      double t2 = omp_get_wtime()*1000.0;

      // get the matrix square root
      LLT<MatrixXd> chol((L+lambda)*(P_t));
      MatrixXd sqrt = chol.matrixL(); // chol

      std::cout<<"\n\nCorrection sqrt(P_T):\n"<< sqrt.diagonal().transpose().format(CleanFmt) <<"\n\n";
      double t3 = omp_get_wtime()*1000.0;

      // perturb x with covariance values => make sigma point vectors
      if (NUMBER_CHECK) {
        std::cout<<"\n\nCorrection sqrt(P_T):\n"<< sqrt.format(CleanFmt) <<"\n\n";
        printf("Correct: Before Step at time %f\n", sigma_states[0]->time);
        for (int i=0; i<N; i++) {
          printf("%d\t", i);
          for (int j=0; j<nq; j++)
            printf("%1.5f ", sigma_states[i]->qpos[j]);
          for (int j=0; j<nv; j++)
            printf("%1.5f ", sigma_states[i]->qvel[j]);
          printf("\t:: ");
          for (int j=0; j<nu; j++)
            printf("%1.5f ", sigma_states[i]->ctrl[j]);
          printf("\t:: ");
          for (int j=0; j<m->nsensordata; j++)
            printf("%f ", sigma_states[i]->sensordata[j]);
          printf("\n");
        }
      }

#pragma omp parallel for schedule(static, 10)
      for (int i=1; i<=L; i++) {
        //x[i+0] += sqrt.col(i-1);
        //x[i+L] -= sqrt.col(i-1);

        x[i+0] = x[0]+sqrt.col(i-1);
        x[i+L] = x[0]-sqrt.col(i-1);
        //x[i+0] = x[0];
        //x[i+L] = x[0];

        mju_copy(sigma_states[i]->qpos,   &x[i](0), nq);
        mju_copy(sigma_states[i]->qvel,   &x[i](nq), nv);
        //mju_copy(sigma_states[i]->qacc, d->qacc, nv);
        mju_copy(sigma_states[i+L]->qpos, &x[i+L](0), nq);
        mju_copy(sigma_states[i+L]->qvel, &x[i+L](nq), nv);
        //mju_copy(sigma_states[i+L]->qacc, d->qacc, nv);
      }
      double t4 = omp_get_wtime()*1000.0;

      // copy back to sigma_states and step forward in time
#pragma omp parallel for schedule(static, 10)
      for (int i=0; i<N; i++) {
        mj_forward(m, sigma_states[i]); // TODO use forward_skip for non_qpos changes
        mj_sensor(m, sigma_states[i]);

        gamma[i] = Map<VectorXd>(sigma_states[i]->sensordata, m->nsensordata);

        //VectorXd r = VectorXd::Zero(m->nsensordata).unaryExpr(&UKF::sensor_sample);
        //gamma[i] = gamma[i] + r;
        //mju_copy(&gamma[i](0), sigma_states[i]->sensordata, m->nsensordata);
        // TODO can do W_s multiply here
      }

      if (NUMBER_CHECK) {
        printf("Correct: After Step time %f\n", sigma_states[0]->time);
        for (int i=0; i<N; i++) {
          printf("%d\t", i);
          for (int j=0; j<nq; j++)
            printf("%1.5f ", sigma_states[i]->qpos[j]);
          for (int j=0; j<nv; j++)
            printf("%1.5f ", sigma_states[i]->qvel[j]);
          printf("\t:: ");
          for (int j=0; j<nu; j++)
            printf("%1.5f ", sigma_states[i]->ctrl[j]);
          printf("\t:: ");
          for (int j=0; j<m->nsensordata; j++)
            printf("%f ", sigma_states[i]->sensordata[j]);
          printf("\n");
        }
      }

      printf("\n");
      double t5 = omp_get_wtime()*1000.0;

      VectorXd z_k = VectorXd::Zero(m->nsensordata);
      /*
         for (int i=1; i<N; i++) {
         z_k += gamma[i];
         }
         z_k = (W_s[0]*gamma[0]) + ((N-1)*W_s[1]*z_k); 
         */
      for (int i=0; i<N; i++) {
        z_k +=W_s[i]*gamma[i];
      }
      if (NUMBER_CHECK) {
        std::cout << "\nz_k hat:\n"<< (z_k).transpose().format(CleanFmt) << std::endl;
      }

      P_z.setZero();
      Pxz.setZero();
      for (int i=0; i<N; i++) {
        VectorXd z(gamma[i] - z_k);
        VectorXd x_i(x[i] - x_t);

        P_z += W_c[i] * (z * z.transpose());
        Pxz += W_c[i] * (x_i * z.transpose());
      }
      /*
         for (int i=1; i<N; i++) {
         VectorXd z(gamma[i] - z_k);
         VectorXd x_i(x[i] - x_t);

         P_z += (z * z.transpose());
         Pxz += (x_i * z.transpose());
         }
         VectorXd z(gamma[0] - z_k);
         VectorXd x_i(x[0] - x_t);
         P_z = (W_c[0] * (z * z.transpose())) + ((N-1)*W_c[1] * P_z);
         Pxz = (W_c[0] * (x_i * z.transpose())) + ((N-1)*W_c[1] * Pxz);
         */

      for (int i=0; i<m->nsensordata; i++) {
        if (P_z.row(i).isZero(1e-9)) {
          P_z.row(i).setZero();
          P_z.col(i).setZero();
          P_z(i,i) = 1.0;
        }
      }

      MatrixXd K = Pxz * P_z.inverse();

      VectorXd s = Map<VectorXd>(sensors, m->nsensordata); // map our real z to vector
      x_t = x_t + (K*(s-z_k));
      P_t = P_t - (K * P_z * K.transpose());

      double t6 = omp_get_wtime()*1000.0;

      printf("correct copy %f, sqrt %f, sigmas %f, mjsteps %f, merge %f\n",
          t2-t1, t3-t2, t4-t3, t5-t4, t6-t5);

      if (NUMBER_CHECK) {
        std::cout << "Kalman Gain:\n"<< K.format(CleanFmt) << std::endl;
        std::cout << "\nPz  :\n"<< P_z.format(CleanFmt) << std::endl;
        std::cout << "\nPz^-1:\n"<< P_z.inverse().format(CleanFmt) << std::endl;
        std::cout << "\nPxz :\n"<< Pxz.format(CleanFmt) << std::endl;
        std::cout << "\ncorrect p_t :\n"<< P_t.format(CleanFmt) << std::endl;
        std::cout << "\ncorrect x_t:\n"<< x_t.transpose().format(CleanFmt) << std::endl;
        std::cout << "\nraw sensors:\n"<< (s).transpose().format(CleanFmt) << std::endl;
        std::cout << "\nz_k hat:\n"<< (z_k).transpose().format(CleanFmt) << std::endl;
        std::cout << "\ndelta:\n"<< (s-z_k).transpose().format(CleanFmt) << std::endl;
        std::cout << "OOOOOOOOOOOOOOOOOOOOOOOOOOOOOOLLLDD CORRECTION STEP\n";
      }

      // set our ideal state to be our calculated estimate
      mju_copy(d->qpos, &(x_t(0)), nq);
      mju_copy(d->qvel, &(x_t(nq)), nv);

    }

    mjData* get_state() {
      return this->d;
    }

    std::vector<mjData*> get_sigmas() {
      return sigma_states;
    }

    mjData* get_stddev() {
      VectorXd var = P_t.diagonal();
      if (NUMBER_CHECK) {
        std::cout<<"P_T diag:\n";
        std::cout<< var.transpose() << std::endl;
        std::cout<<"\nP_z diag:\n";
        std::cout<< P_z.diagonal().transpose() << std::endl;
      }
      mju_copy(stddev->qpos, &(var(0)), nq);
      mju_copy(stddev->qvel, &(var(nq)), nv);
      if (ctrl_state) mju_copy(stddev->ctrl, &(var(nq+nv)), nu);
      else mju_copy(stddev->ctrl, d->ctrl, nu);

      var = P_z.diagonal();
      mju_copy(stddev->sensordata, &(var(0)), m->nsensordata);

      return stddev;
    }


  private:
    int L;
    int N;
    double alpha;
    double beta;
    double lambda;
    double diag;
    double Ws0;
    double noise;
    double * W_s;
    double * W_c;
    double * p_state;
    std::vector<double *> raw; // raw data storage pointed to by x
    //std::vector<VectorXd *> x;
    VectorXd * x;
    VectorXd * gamma;
    VectorXd x_t;
    //MatrixXd * P;
    MatrixXd P_t;
    MatrixXd P_z;
    MatrixXd Pxz;

    std::vector<mjData *> sigma_states;
    std::vector<mjData *> sigmas;
    mjData * stddev;
    //mjData ** sigma_states;
    //mjData ** sigmas;

    bool NUMBER_CHECK;
    bool ctrl_state;
};




