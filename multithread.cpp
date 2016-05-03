#include "mujoco.h"
#include <stdlib.h>
#include <cstdlib>
#include <stdio.h>
#include <omp.h>


// mjData for maximum number of threads
mjData* dthread[32];
mjData* d;
mjModel* m;

void all_in_one(int N) {
  double T0 = omp_get_wtime();
#pragma omp parallel
  {
    // get thread id
    int tid = omp_get_thread_num();

    // copy qacc, qvel, qpos from d; clear ctrl
    mju_copy(dthread[tid]->qacc, d->qacc, m->nv);
    mju_copy(dthread[tid]->qvel, d->qvel, m->nv);
    mju_copy(dthread[tid]->qpos, d->qpos, m->nq);
    mju_zero(dthread[tid]->ctrl, m->nu);

    // forward: reconstruct d without copy
    mj_forward(m, dthread[tid]);

    // schedule evaluations for this thread
    int cchunk = (m->nu + N-1) / N;
    int cstart = tid * cchunk;
    int cend = mjMIN(cstart+cchunk, m->nu);
    int vchunk = (m->nv + N-1) / N;
    int vstart = tid * vchunk;
    int vend = mjMIN(vstart+vchunk, m->nv);
    int qchunk = (m->nq + N-1) / N;
    int qstart = tid * qchunk;
    int qend = mjMIN(qstart+qchunk, m->nq);
    int i;

    // control evaluations
    for( i=cstart; i<cend; i++ )
    {
      // copy qacc from d; clear ctrl
      mju_copy(dthread[tid]->qacc, d->qacc, m->nv);
      mju_zero(dthread[tid]->ctrl, m->nu);

      // perturb ctrl[i] by +epsilon, eval
      dthread[tid]->ctrl[i] = 0.01;
      mj_forwardSkip(m, dthread[tid], 2);

      // perturb ctrl[i] by -epsilon, eval
      dthread[tid]->ctrl[i] = -0.01;
      mj_forwardSkip(m, dthread[tid], 2);
    }

    // velocity evaluations
    for( i=vstart; i<vend; i++ )
    {
      // copy qacc, qvel from d; clear ctrl
      mju_copy(dthread[tid]->qacc, d->qacc, m->nv);
      mju_copy(dthread[tid]->qvel, d->qvel, m->nv);
      mju_zero(dthread[tid]->ctrl, m->nu);

      // perturb qvel[i] by +epsilon, eval
      dthread[tid]->qvel[i] += 0.01;
      mj_forwardSkip(m, dthread[tid], 1);

      // perturb qvel[i] by -epsilon, eval
      dthread[tid]->qvel[i] -= 0.02;
      mj_forwardSkip(m, dthread[tid], 1);
    }

    // position evaluations
    for( i=qstart; i<qend; i++ )
    {
      // copy qacc, qvel, qpos from d; clear ctrl
      mju_copy(dthread[tid]->qacc, d->qacc, m->nv);
      mju_copy(dthread[tid]->qvel, d->qvel, m->nv);
      mju_copy(dthread[tid]->qpos, d->qpos, m->nq);
      mju_zero(dthread[tid]->ctrl, m->nu);

      // perturb qpos[i] by +epsilon, eval (forward will normalize quaternions)
      dthread[tid]->qpos[i] += 0.01;
      mj_forwardSkip(m, dthread[tid], 0);

      // recover qpos (since quaternions were normalized)
      mju_copy(dthread[tid]->qpos, d->qpos, m->nq);

      // perturb qpos[i] by -epsilon, eval (forward will normalize quaternions)
      dthread[tid]->qpos[i] -= 0.01;
      mj_forwardSkip(m, dthread[tid], 0);
    }
  }
  double T1 = omp_get_wtime();
  printf("all-in-one  %.2f\n\n", (T1-T0)*1e+3);
}

void way_1(int N) {
  double t0 = omp_get_wtime();

  // set all dthread = d, so we can reuse results
#pragma omp parallel 
  {
    // get thread id
    int tid = omp_get_thread_num();
    // copy qacc, qvel, qpos from center point; clear ctrl
    mju_copy(dthread[tid]->qacc, d->qacc, m->nv);
    mju_copy(dthread[tid]->qvel, d->qvel, m->nv);
    mju_copy(dthread[tid]->qpos, d->qpos, m->nq);
    mju_zero(dthread[tid]->ctrl, m->nu);
    // forward - reconstruct d without copy (this is faster)
    mj_forward(m, dthread[tid]);
  }

  double t1 = omp_get_wtime();

  // eval samples that change control
#pragma omp parallel for
  for( int i=0; i<m->nu; i++ ) {
    // get thread id
    int tid = omp_get_thread_num();

    // copy qacc from center point; clear ctrl
    mju_copy(dthread[tid]->qacc, d->qacc, m->nv);
    mju_zero(dthread[tid]->ctrl, m->nu);

    // perturb ctrl[i] by +epsilon, eval
    dthread[tid]->ctrl[i] = 0.01;
    mj_forwardSkip(m, dthread[tid], 2);

    // perturb ctrl[i] by -epsilon, eval
    dthread[tid]->ctrl[i] = -0.01;
    mj_forwardSkip(m, dthread[tid], 2);
  }

  double t2 = omp_get_wtime();

  // eval samples that change velocity
#pragma omp parallel for
  for( int i=0; i<m->nv; i++ ) {
    // get thread id
    int tid = omp_get_thread_num();

    // copy qacc, qvel from center point; clear ctrl
    mju_copy(dthread[tid]->qacc, d->qacc, m->nv);
    mju_copy(dthread[tid]->qvel, d->qvel, m->nv);
    mju_zero(dthread[tid]->ctrl, m->nu);

    // perturb qvel[i] by +epsilon, eval
    dthread[tid]->qvel[i] += 0.01;
    mj_forwardSkip(m, dthread[tid], 1);

    // perturb qvel[i] by -epsilon, eval
    dthread[tid]->qvel[i] -= 0.02;
    mj_forwardSkip(m, dthread[tid], 1);
  }

  double t3 = omp_get_wtime();

  // eval samples that change position
#pragma omp parallel for
  for( int i=0; i<m->nq; i++ ) {
    // get thread id
    int tid = omp_get_thread_num();

    // copy qacc, qvel, qpos from center point; clear ctrl
    mju_copy(dthread[tid]->qacc, d->qacc, m->nv);
    mju_copy(dthread[tid]->qvel, d->qvel, m->nv);
    mju_copy(dthread[tid]->qpos, d->qpos, m->nq);
    mju_zero(dthread[tid]->ctrl, m->nu);

    // perturb qpos[i] by +epsilon, eval (forward will normalize quaternions)
    dthread[tid]->qpos[i] += 0.01;
    mj_forwardSkip(m, dthread[tid], 0);

    // recover qpos (since quaternions were normalized)
    mju_copy(dthread[tid]->qpos, d->qpos, m->nq);

    // perturb qpos[i] by -epsilon, eval (forward will normalize quaternions)
    dthread[tid]->qpos[i] -= 0.01;
    mj_forwardSkip(m, dthread[tid], 0);
  }

  double t4 = omp_get_wtime();

  printf("copy (%2d)  %.2f\nctrl (%2d)  %.2f\nqvel (%2d)  %.2f\nqpos (%2d)  %.2f\n\ntotal  %.2f\n\n",
      N, (t1-t0)*1e+3, 
      2*m->nu, (t2-t1)*1e+3, 
      2*m->nv, (t3-t2)*1e+3, 
      2*m->nq, (t4-t3)*1e+3, (t4-t0)*1e+3);
}

int main(int argc, const char** argv) {
  // fixed number of threads = number of logical cores
  int N = mjMIN(32, omp_get_num_procs());
  omp_set_dynamic(0);

  if (argc > 1)
    N = std::atoi(argv[1]);

  omp_set_num_threads(N);

  printf("Num Threads %d\n\n", N);

  // activate and load model
  mj_activate("mjkey.txt");
  m = mj_loadXML("../models/humanoid.xml", NULL, 0, 0);
  //	mjModel* m = mj_loadModel("darwin.mjb", NULL, 0);
  if( !m )
    return 1;

  // make data: main plus per-thread
  d = mj_makeData(m);
  for( int n=0; n<N; n++ )
    dthread[n] = mj_makeData(m);

  // simulate for 5 sec, settle into state
  while( d->time<5 )
    mj_step(m, d);

  // set options for sampling
  m->opt.iterations = 20;
  m->opt.tolerance = 0;


  // all-in-one, internal scheduling
  all_in_one(N);
  all_in_one(N);
  all_in_one(N);

  way_1(N);
  way_1(N);
  way_1(N);


  // shut down
  mj_deleteData(d);
  for( int n=0; n<N; n++ )
    mj_deleteData(dthread[n]);
  mj_deleteModel(m);
  mj_deactivate();

  //getchar();
  return 0;
}