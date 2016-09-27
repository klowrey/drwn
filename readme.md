
darwin meshes
https://www.thingiverse.com/thing:9793/#files


todo:

collect raw data again that is better


figure out P_t and P_z variance (diagonal) values that correctly represent the
state and sensor variances -- this is kind of like the Rao-Blackwellization



process for offline:
filter contact sensors?
clean marker data (velocity filter and bounding box filter)
covariance matrix needs diagonal values
root pos, joint pos, root vel, joint vel for state



07/15

PzAdd seems totally critical, even with sensor noise
PtAdd contributes to more stable estimation, but may be un-realistic?

even weighting makes things more stable

**** Ctrl noise does not make too much difference it seems in increasing the state 
**** covariance
      More control noise allows for the 'process' noise to increase a bit->
        this means more allowance for the sensors to correct and is somewhat
        needed (1e-3, 1e-4)

qhat is also somewhat neutral to helpful
    shat and qhat seem to go together; maybe they just cancel out all effects?
    shat and qhat help when there is sensor noise injected into the estimates

constraint scaling seems to cause harm, but when PtAdd / covar is large, it
seems to mitigate some effects?
    Maybe constraint scaling just does not matter when using even weighting

TODO seem to be missing some senors?


07/17

ctrl noise is 1e-3, 1e-4 for walk4.csv
qhat and shat are zero'd out, with 1e10 on the phasespace sensors
 <numeric name="snsr_covar" size="15" data="
 1e-2 1e-4 1e-0 1e-0 1e-6  1e-2 1e-0
 1e-0 1e-0 1e-0 1e-0 1e+10 1e-0 1e-0 1e-0" />

Making the contacts softer doesn't change the effects; aka the friction is not
critical to the correct stepping behavior. Forward walking still appears due to
the other sensors more so than the robot actually stepping.

even weighting, no scaling
./viewer -m ../models/darwin.xml -r 1 -f walk3.csv -d 1e-3 -i -1 -t 6 -n 0

Model needs to be sped up:
  less boxes?
  spheres instead?


07/18
reducing condim helps to make things slightly faster, but a little more
unstable?
  the friction / torquing helps the robot stay upright

adding sensor noise does not seem to matter that much; probably the effects
  are overwritten by the pzadd

For more perf:
  Do predict_correct_p1 while data is collecting
    when data is ready, do predict correct p2


07/20
Paper organization:
    intro
    related works
      DRC papers
      limitations of MHE
      limitations of splitting the estimator in two
    problem description
      process noise is hard to quantify
      modelling errors are always present
      contacts as discontinuities means that traditional kalman methods are difficult
    UKF and process noise / ensemble
      even weighting vs traditional weighting
        -- why does traditional fail?
      PzAdd and sensor noise
      time noise
      control noise
      model noise == ensemble method, but gives us our process noise
    robot platform
      Darwin, 26 dof, etc etc.
    Methods
      Running online
      Comparing offline data with different parameters
      Plot of real walking data, distance vs UKF styles
        minimal process noise
        increasing process noise from ensemble process
    results
      'the ensembled ukf is great'
    discussion & conclusion
    future work

run benchmarks single threaded--the random values and threading 
must be causing additional variance in the estimation

RMSE: only use markers visible the whole time? more model tweaking? make
contacts harder again?

07/21
Have plots showing how the model noise helps reduce RMSE, but i am not sure if
the phasespace rmse is very accurate of a measure of success.

07/26
LOO plot with real data; include contact / torque more?
scaled model noise plot: summed RMSE vs amount of noise of TMC model
include phasespace data? 

make plot that does an overhead view of distance travelled?

adding sensor noise? needed

collect falling data
  collected; re-do RMSE for falling / fallen model
    this means cleaning the csv files

RMSE units: Meters


07/27
todo list:
  produce final LOO w/ real data
  produce diminishing returns plot
