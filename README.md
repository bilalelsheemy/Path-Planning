# Path-Planning
This project is part of the Udacity Self-driving Nano-degree.
This pipeline is consisted mainly of two main parts: 
1. Behavior Planning:
Responsible for deciding which maneuver should be followed each time.

2. Trajectory Generation.
Responsible for generating a proper set of points represents the trajectory that achieve the maneuver decided by the behavior planner.

### Behavior Planner
The strategy here is based on the following approach: 

1. If the ego vehicle doesn't get obstructed by traffic in-front of it, then it will continue in its current lane trying to stick to the maximum possible speed which is 49.5 mph.

2. When it gets obstructed by traffic the planner will do the following:
	* Parse the data from sensor fusion to track all the vehicles that are within specific range of S (in Frenet coordinates). 
	* Categorize these vehicles according to their respective lanes.
	* Assessing the possibility of doing a lane change for each lane.
	* Upon this assessment, if it's not possible to do any lane change it will continue in its lane sticking to the maximum possible speed 		which is the target vehicle speed in this case. 
	* If there're possible lane changes then, the planner will select the appropriate change in order to pass it to the trajectory 		generator to execute this maneuver.
	* The behavior planner takes into the consideration the followings while deciding the next maneuver:
		* Maneuver Safety (Collision Avoidance). 
		* Sticking to the Max possible speed.
		* Avoiding being outside the lane for more than 3 seconds.

### Trajectory Generator
The strategy here is concerned with the followings: 

1. Making the transition from the old to the new path as smooth as possible. 
This is reached in this pipeline through the following:
	* If there're some points carried over from the previous path, they will be used as the starting points in the new path. 
	* If there were no points available from the previous path, the generator will used the ego vehicle orientation (yaw angle) to make 		the new path tangent to the end of the previous path.

2. Generate a set of point with a proper spacing to control the ego vehicle speed as required. 
In order to achieve this a rough path was initially created based on the smooth transition concept and then three new points were added to this path. The new points are 30m spaced to allow for spline utilization which will help interpolating points between the known way points. 

* We call this rough path points the anchor points since we will build the spline on them (These points are the input to spline).

* The actual path was selected to contain 50 points starting usually by the previous path carry over (i.e. the unprocessed points from the previous path) then the spline generated points from the anchor points. 

### Jerk & Acceleration Minimizing
To guarantee the acceleration & jerk to be under the required thresholds 10m/s^2, 10m/s^3 a soft start was forced at the beginning of the vehicle accelerating and in any other instance when the ego vehicle is required to accelerate or decelerate according to the surrounding traffic. 
The speed incremental/decrement value was tuned multiple times started from (0.1 mph) till (0.224 mph) which was found to be on the fence of the range which will not cause acceleration or jerk more than 10 m/s^2 or 10 m/s^3.
Accordingly the maximum acceleration was set to 0.224 and the max deceleration  was set to -0.2 based on multiple tunings on the simulator.


### Future Improvements
The pipeline is flexible for any further optimizations.
For example: 
* Implementing a multiple lanes crossing maneuver.
	* One of the potential enhancements for this pipeline is utilizing a maneuver for transitioning from lane 0 to lane 2 and vice versa.  
   

