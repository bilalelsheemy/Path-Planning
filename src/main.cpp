#include <fstream>
#include <math.h>
#include <uWS/uWS.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "json.hpp"
#include "spline.h"
#include <math.h>

using namespace std;

// for convenience
using json = nlohmann::json;

// For converting back and forth between radians and degrees.
constexpr double pi() { return M_PI; }
double deg2rad(double x) { return x * pi() / 180; }
double rad2deg(double x) { return x * 180 / pi(); }

// Checks if the SocketIO event has JSON data.
// If there is data the JSON object in string format will be returned,
// else the empty string "" will be returned.
string hasData(string s) {
  auto found_null = s.find("null");
  auto b1 = s.find_first_of("[");
  auto b2 = s.find_first_of("}");
  if (found_null != string::npos) {
    return "";
  } else if (b1 != string::npos && b2 != string::npos) {
    return s.substr(b1, b2 - b1 + 2);
  }
  return "";
}

double distance(double x1, double y1, double x2, double y2)
{
	return sqrt((x2-x1)*(x2-x1)+(y2-y1)*(y2-y1));
}
int ClosestWaypoint(double x, double y, const vector<double> &maps_x, const vector<double> &maps_y)
{

	double closestLen = 100000; //large number
	int closestWaypoint = 0;

	for(int i = 0; i < maps_x.size(); i++)
	{
		double map_x = maps_x[i];
		double map_y = maps_y[i];
		double dist = distance(x,y,map_x,map_y);
		if(dist < closestLen)
		{
			closestLen = dist;
			closestWaypoint = i;
		}

	}

	return closestWaypoint;

}

int NextWaypoint(double x, double y, double theta, const vector<double> &maps_x, const vector<double> &maps_y)
{

	int closestWaypoint = ClosestWaypoint(x,y,maps_x,maps_y);

	double map_x = maps_x[closestWaypoint];
	double map_y = maps_y[closestWaypoint];

	double heading = atan2((map_y-y),(map_x-x));

	double angle = fabs(theta-heading);
  angle = min(2*pi() - angle, angle);

  if(angle > pi()/4)
  {
    closestWaypoint++;
  if (closestWaypoint == maps_x.size())
  {
    closestWaypoint = 0;
  }
  }

  return closestWaypoint;
}

// Transform from Cartesian x,y coordinates to Frenet s,d coordinates
vector<double> getFrenet(double x, double y, double theta, const vector<double> &maps_x, const vector<double> &maps_y)
{
	int next_wp = NextWaypoint(x,y, theta, maps_x,maps_y);

	int prev_wp;
	prev_wp = next_wp-1;
	if(next_wp == 0)
	{
		prev_wp  = maps_x.size()-1;
	}

	double n_x = maps_x[next_wp]-maps_x[prev_wp];
	double n_y = maps_y[next_wp]-maps_y[prev_wp];
	double x_x = x - maps_x[prev_wp];
	double x_y = y - maps_y[prev_wp];

	// find the projection of x onto n
	double proj_norm = (x_x*n_x+x_y*n_y)/(n_x*n_x+n_y*n_y);
	double proj_x = proj_norm*n_x;
	double proj_y = proj_norm*n_y;

	double frenet_d = distance(x_x,x_y,proj_x,proj_y);

	//see if d value is positive or negative by comparing it to a center point

	double center_x = 1000-maps_x[prev_wp];
	double center_y = 2000-maps_y[prev_wp];
	double centerToPos = distance(center_x,center_y,x_x,x_y);
	double centerToRef = distance(center_x,center_y,proj_x,proj_y);

	if(centerToPos <= centerToRef)
	{
		frenet_d *= -1;
	}

	// calculate s value
	double frenet_s = 0;
	for(int i = 0; i < prev_wp; i++)
	{
		frenet_s += distance(maps_x[i],maps_y[i],maps_x[i+1],maps_y[i+1]);
	}

	frenet_s += distance(0,0,proj_x,proj_y);

	return {frenet_s,frenet_d};

}

// Transform from Frenet s,d coordinates to Cartesian x,y
vector<double> getXY(double s, double d, const vector<double> &maps_s, const vector<double> &maps_x, const vector<double> &maps_y)
{
	int prev_wp = -1;

	while(s > maps_s[prev_wp+1] && (prev_wp < (int)(maps_s.size()-1) ))
	{
		prev_wp++;
	}

	int wp2 = (prev_wp+1)%maps_x.size();

	double heading = atan2((maps_y[wp2]-maps_y[prev_wp]),(maps_x[wp2]-maps_x[prev_wp]));
	// the x,y,s along the segment
	double seg_s = (s-maps_s[prev_wp]);

	double seg_x = maps_x[prev_wp]+seg_s*cos(heading);
	double seg_y = maps_y[prev_wp]+seg_s*sin(heading);

	double perp_heading = heading-pi()/2;

	double x = seg_x + d*cos(perp_heading);
	double y = seg_y + d*sin(perp_heading);

	return {x,y};

}

double mph2mps(double speed_mph){
  double speed_mps = speed_mph/2.24;
  return speed_mps;
}

/*** Defining some required params ***/
double ref_vel = 0; // mph
double dt = 0.02; //seconds
int lane = 1; // Initial lane
int outOflane_cost = 0;
int lane_changed = 0;
int stay_inlane_cntr = 0;

int main() {
  uWS::Hub h;

  // Load up map values for waypoint's x,y,s and d normalized normal vectors
  vector<double> map_waypoints_x;
  vector<double> map_waypoints_y;
  vector<double> map_waypoints_s;
  vector<double> map_waypoints_dx;
  vector<double> map_waypoints_dy;

  // Waypoint map to read from
  string map_file_ = "../data/highway_map.csv";
  // The max s value before wrapping around the track back to 0
  double max_s = 6945.554;

  ifstream in_map_(map_file_.c_str(), ifstream::in);

  string line;
  while (getline(in_map_, line)) {
  	istringstream iss(line);
  	double x;
  	double y;
  	float s;
  	float d_x;
  	float d_y;
  	iss >> x;
  	iss >> y;
  	iss >> s;
  	iss >> d_x;
  	iss >> d_y;
  	map_waypoints_x.push_back(x);
  	map_waypoints_y.push_back(y);
  	map_waypoints_s.push_back(s);
  	map_waypoints_dx.push_back(d_x);
  	map_waypoints_dy.push_back(d_y);
  }


  h.onMessage([&map_waypoints_x,&map_waypoints_y,&map_waypoints_s,&map_waypoints_dx,&map_waypoints_dy](uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
                     uWS::OpCode opCode) {
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    //auto sdata = string(data).substr(0, length);
    //cout << sdata << endl;
    if (length && length > 2 && data[0] == '4' && data[1] == '2') {

      auto s = hasData(data);

      if (s != "") {
        auto j = json::parse(s);

        string event = j[0].get<string>();

        if (event == "telemetry") {
          // j[1] is the data JSON object

        	// Main car's localization Data
          	double car_x = j[1]["x"];
          	double car_y = j[1]["y"];
          	double car_s = j[1]["s"];
          	double car_d = j[1]["d"];
          	double car_yaw = j[1]["yaw"];
          	double car_speed = j[1]["speed"];

          	// Previous path data given to the Planner
          	auto previous_path_x = j[1]["previous_path_x"];
          	auto previous_path_y = j[1]["previous_path_y"];
          	// Previous path's end s and d values
          	double end_path_s = j[1]["end_path_s"];
          	double end_path_d = j[1]["end_path_d"];

          	// Sensor Fusion Data, a list of all other cars on the same side of the road.
          	auto sensor_fusion = j[1]["sensor_fusion"];

          	json msgJson;

            // TODO: define a path made up of (x,y) points that the car will visit sequentially every .02 seconds
            vector<double> next_x_vals;
          	vector<double> next_y_vals;


            /*** Start of the Pipeline  ***/
            int prev_path_size = previous_path_x.size();
            int target_tooClose = 0; // Clearing the lane change triggering flag
            vector<vector<double>> targets_Infront;

            // Trying to start the new path tangent to the previous one
            if(prev_path_size > 0)
            {
              car_s = end_path_s;
            }


            // Check for the targets infront of ego veh
            for(int i=0; i < sensor_fusion.size(); i++)
            {
              float target_d = sensor_fusion[i][6];
              double target_s = sensor_fusion[i][5];
              double target_vx = sensor_fusion[i][3];
              double target_vy = sensor_fusion[i][4];
              double target_speed = sqrt(pow(target_vx,2) + pow(target_vy, 2));
              double target_updated_s = target_s + ((double)prev_path_size*target_speed*dt);

              // Close target veh infront of the ego veh
              if((target_d > (2+4*lane-2)) && (target_d < (2+4*lane+2)))
              {
                if((target_updated_s > car_s) && ((target_updated_s-car_s) < 30.0))
                {
                  target_tooClose = 1;
                  targets_Infront.push_back(sensor_fusion[i]);
                }
              }
            }

            // Handling being obstructed by the traffic
            // Possible solutions are either do a lane change to a faster lane or just follow the
            // current target vehicle with its speed.
            if(target_tooClose)
            {
              ref_vel -= 0.224; //decrement ego veh speed by 1.5 mph

              vector<vector<double>> lane0_vehs;
              vector<vector<double>> lane1_vehs;
              vector<vector<double>> lane2_vehs;

              for(int i=0; i < sensor_fusion.size(); i++)
              {
                float target_d = sensor_fusion[i][6];
                double target_s = sensor_fusion[i][5];
                double target_vx = sensor_fusion[i][3];
                double target_vy = sensor_fusion[i][4];
                double target_speed = sqrt(pow(target_vx,2) + pow(target_vy, 2));
                double target_updated_s = target_s + ((double)prev_path_size*target_speed*dt);
                sensor_fusion[i].push_back(target_updated_s);
                sensor_fusion[i].push_back(target_speed);

                // Parse a reasonable range behind and infront of the ego veh in all the lanes
                if((car_s-60.0 < target_updated_s) &&  (target_updated_s < car_s+60.0))
                {
                  // Categorize the target vehicles according to their respective lanes
                  if((0.0 < target_d) && (target_d < 4.0))
                  {
                    lane0_vehs.push_back(sensor_fusion[i]);
                  }
                  else if((4.0 < target_d) && (target_d < 8.0))
                  {
                    lane1_vehs.push_back(sensor_fusion[i]);
                  }
                  else if((8.0 < target_d) && (target_d < 12.0))
                  {
                    lane2_vehs.push_back(sensor_fusion[i]);
                  }
                }
              }


              // Assess the feasability of doing a lane change
              if(!lane_changed)
              {
                // Middle Lane Assessment
                int middlelane_available = 0;
                double middlelane_speed;
                if(lane1_vehs.size() != 0)
                {
                  for(int i=0; i < lane1_vehs.size(); i++)
                  {
                    if((car_s-20.0 < lane1_vehs[i][7]) &&  (lane1_vehs[i][7] < car_s+20.0))
                    {
                      middlelane_available = 0;
                    }
                    else if(lane1_vehs[i][7] > car_s+20.0)
                    {
                      if(lane1_vehs[i][8] > car_speed)
                      {
                        middlelane_available = 1;
                        middlelane_speed = lane1_vehs[i][8];
                      }
                      else
                      {
                        middlelane_available = 0;
                      }
                    }
                    else if(lane1_vehs[i][7] < car_s-30.0)
                    {
                      middlelane_available = 1;
                    }
                  }
                }
                else
                {
                  middlelane_available = 1;
                }

                // Left Lane Assessment
                int leftlane_available = 0;
                double leftlane_speed;
                if(lane0_vehs.size() != 0)
                {
                  for(int i=0; i < lane0_vehs.size(); i++)
                  {
                    if((car_s-20.0 < lane0_vehs[i][7]) &&  (lane0_vehs[i][7] < car_s+20.0))
                    {
                      leftlane_available = 0;
                    }
                    else if(lane0_vehs[i][7] > car_s+20.0)
                    {
                      if(lane0_vehs[i][8] > car_speed)
                      {
                        leftlane_available = 1;
                        leftlane_speed = lane0_vehs[i][8];
                      }
                      else
                      {
                        leftlane_available = 0;
                      }
                    }
                    else if(lane0_vehs[i][7] < car_s-30.0)
                    {
                      leftlane_available = 1;
                      leftlane_speed = 49.5;
                    }
                  }
                }
                else
                {
                  leftlane_available = 1;
                  leftlane_speed = 49.5;
                }




                // Right Lane Assessment
                int rightlane_available = 0;
                double rightlane_speed;
                if(lane2_vehs.size() != 0)
                {
                  for(int i=0; i < lane2_vehs.size(); i++)
                  {
                    if((car_s-20.0 < lane2_vehs[i][7]) &&  (lane2_vehs[i][7] < car_s+20.0))
                    {
                      rightlane_available = 0;
                    }
                    else if(lane2_vehs[i][7] > car_s+20.0)
                    {
                      if(lane2_vehs[i][8] > car_speed)
                      {
                        rightlane_available = 1;
                        rightlane_speed = lane2_vehs[i][8];
                      }
                      else
                      {
                        rightlane_available = 0;
                      }
                    }
                    else if(lane2_vehs[i][7] < car_s-30.0)
                    {
                      rightlane_available = 1;
                      rightlane_speed = 49.5;
                    }
                  }
                }
                else
                {
                  rightlane_available = 1;
                  rightlane_speed = 49.5;
                }

                // Forcing a lane keep mode in case of no lane changes are available.
                if(!leftlane_available && !middlelane_available && !rightlane_available)
                {
                  double min_s = 100.0; //just a very large number
                  double nearest_target_speed;

                  for(int i=0; i < targets_Infront.size(); i++)
                  {

                    if(targets_Infront[i][7] < min_s)
                    {
                      min_s = targets_Infront[i][7];
                      int nearest_target_idx = i;
                      nearest_target_speed = targets_Infront[i][8];
                    }
                  }
                  // Following the target veh infornt with its speed (max possible speed in this case)
                  if(ref_vel < nearest_target_speed)
                  {
                    // This incemental value corresponds to acceleration < 10m\s^2
                    // Using this value in accelerating/decelerating the vehicle shall guarantee
                    // the jerk minimization.
                    ref_vel += 0.224;
                  }

                  else if(ref_vel > nearest_target_speed)
                  {
                    // This decrement value corresponds to acceleration < 10m\s^2
                    // Using this value in accelerating/decelerating the vehicle shall guarantee
                    // the jerk minimization.
                    // The decelerating rate is almost double the acceleration rate to avoid some collision scenarios
                    // happened in the traffic jam and also not too large to guarantee the jerk minimization.
                    // This value was tested multiple times on the simulator and the car drove safely for more than 7 miles.
                    ref_vel -= 0.5;

                  }

                }

                // Make the lane change decision . There must be possible lane changes in this case
                else
                {
                  switch(lane)
                  {
                    case 0:
                      if(middlelane_available)
                      {
                        lane = 1;
                      }
                      lane_changed = 1;
                      break;

                      case 1:
                      if(rightlane_available && leftlane_available)
                      {
                        if(rightlane_speed < leftlane_speed)
                        {
                          lane = 0;
                        }
                        else if(rightlane_speed > leftlane_speed)
                        {
                          lane = 2;
                        }
                        else
                        {
                          lane = 2;
                        }
                      }
                      else if(leftlane_available)
                      {
                        lane = 0;
                      }
                      else if(rightlane_available)
                      {
                        lane = 2;
                      }
                      lane_changed = 1;
                      break;

                      case 2:
                      if(middlelane_available)
                      {
                        lane = 1;
                      }
                      lane_changed = 1;
                      break;
                    }
                  }
                }
              }

            // No close targets infront of ego veh
            else if(ref_vel < 49.5)
            // This incemental value corresponds to acceleration < 10m\s^2 (~9.5m\s^2)
            // Using this value in accelerating/decelerating the vehicle shall guarantee
            // the jerk minimization.
            {
              ref_vel += 0.224;
            }

            // Penalizing frequent lane changes (This is the root cause for being out of lane for
            // more than 3 seconds).
            if(lane_changed && (stay_inlane_cntr < 50))
            {
              stay_inlane_cntr++;
            }
            else
            {
              lane_changed = 0;
              stay_inlane_cntr = 0;
            }

            // Preparing the new path points
            vector<double> ptsx;
            vector<double> ptsy;

            double ref_x = car_x;
            double ref_y = car_y;
            double ref_yaw = deg2rad(car_yaw);


            // To make sure the new path will start tangent to where the car is.
            if(prev_path_size < 2)
            {

              double prev_car_x = car_x - cos(car_yaw);
              double prev_car_y = car_y - sin(car_yaw);

              ptsx.push_back(prev_car_x);
              ptsy.push_back(prev_car_y);

              ptsx.push_back(ref_x);
              ptsy.push_back(ref_y);

             }

             else
             {

               ref_x = previous_path_x[prev_path_size-1];
               ref_y = previous_path_y[prev_path_size-1];

               double prev_ref_x = previous_path_x[prev_path_size-2];
               double prev_ref_y = previous_path_y[prev_path_size-2];

               ref_yaw = atan2(ref_y - prev_ref_y, ref_x - prev_ref_x);

               ptsx.push_back(prev_ref_x);
               ptsy.push_back(prev_ref_y);

               ptsx.push_back(ref_x);
               ptsy.push_back(ref_y);

             }

            // Adding points to the new path spaced 30m each.
            vector<double> next_wp0 = getXY(car_s+30,(2+4*lane), map_waypoints_s, map_waypoints_x, map_waypoints_y);
            vector<double> next_wp1 = getXY(car_s+60,(2+4*lane), map_waypoints_s, map_waypoints_x, map_waypoints_y);
            vector<double> next_wp2 = getXY(car_s+90,(2+4*lane), map_waypoints_s, map_waypoints_x, map_waypoints_y);


            ptsx.push_back(next_wp0[0]);
            ptsx.push_back(next_wp1[0]);
            ptsx.push_back(next_wp2[0]);

            ptsy.push_back(next_wp0[1]);
            ptsy.push_back(next_wp1[1]);
            ptsy.push_back(next_wp2[1]);

            // Coordinates transformation from the map to the car's local Coordinates
            for(int i=0; i < ptsx.size(); i++)
            {
            // Translation
            double shift_x = ptsx[i] - ref_x;
            double shift_y = ptsy[i] - ref_y;

            //Rotation
            ptsx[i] = shift_x * cos(-ref_yaw) - shift_y * sin(-ref_yaw);
            ptsy[i] = shift_x * sin(-ref_yaw) + shift_y * cos(-ref_yaw);
            }

            // Using spline lib to create points closely spaced according to the required car_speed (~50mph)
            tk::spline s;
            s.set_points(ptsx, ptsy);

            double target_x = 30.0;
            double target_y = s(target_x);
            // Ecludian distance to the destination point
            double target_distance = sqrt(pow((target_x), 2)+pow((target_y), 2));

            // Initial X value
            double x_initial = 0.0;

            // Adding the un-processed prev path points for smooth transition between the old & new path
            for(int i=0; i < previous_path_x.size(); i++)
            {
              next_x_vals.push_back(previous_path_x[i]);
              next_y_vals.push_back(previous_path_y[i]);
            }

            // Let's create 50 points path
            for(int i=0; i < 50 - previous_path_x.size(); i++)
            {
              // Determining the number of steps(points) required to cover traget distance given
              //the required speed.
              double N = target_distance / (dt*mph2mps(ref_vel));
              // Generating the closely spaced path points using spline
              double x_point = x_initial + target_x/N;
              double y_point = s(x_point);

              // Updating starting value of x.
              x_initial = x_point;

              // Transform back to the map/global Coordinates
              double dx = x_point;
              double dy = y_point;
              // Rotation
              x_point = dx * cos(ref_yaw) - dy * sin(ref_yaw);
              y_point = dx * sin(ref_yaw) + dy * cos(ref_yaw);

              // Translation
              x_point += ref_x;
              y_point += ref_y;

              next_x_vals.push_back(x_point);
              next_y_vals.push_back(y_point);
            }

            /*** End of the Pipeline ***/

            msgJson["next_x"] = next_x_vals;
          	msgJson["next_y"] = next_y_vals;

          	auto msg = "42[\"control\","+ msgJson.dump()+"]";

          	//this_thread::sleep_for(chrono::milliseconds(1000));
          	ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);

        }
      } else {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
        ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    }
  });

  // We don't need this since we're not using HTTP but if it's removed the
  // program
  // doesn't compile :-(
  h.onHttpRequest([](uWS::HttpResponse *res, uWS::HttpRequest req, char *data,
                     size_t, size_t) {
    const std::string s = "<h1>Hello world!</h1>";
    if (req.getUrl().valueLength == 1) {
      res->end(s.data(), s.length());
    } else {
      // i guess this should be done more gracefully?
      res->end(nullptr, 0);
    }
  });

  h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req) {
   //std::cout << "Connected!!!" << std::endl;
  });

  h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code,
                         char *message, size_t length) {
    ws.close();
   //std::cout << "Disconnected" << std::endl;
  });

  int port = 4567;
  if (h.listen(port)) {
   //std::cout << "Listening to port " << port << std::endl;
  } else {
    std::cerr << "Failed to listen to port" << std::endl;
    return -1;
  }
  h.run();
}