#include "main.h"

#define AVG_ANGLE_COUNT 1

using namespace std;
using namespace cv;


#define LIDAR_MAT_HIGH 600
#define LIDAR_MAT_WIDTH 1000

//rectangle algorithm params
int number_of_rec_cols = 40;
int number_of_rec_raws = 10; //liczba pasów detekcji //s

//mats
Mat test_mat = Mat::zeros(LIDAR_MAT_HIGH, LIDAR_MAT_WIDTH, CV_8UC3 );
Mat output_mat = Mat::zeros(LIDAR_MAT_HIGH, LIDAR_MAT_WIDTH, CV_8UC3 );

Mat  lidar_left_mat = Mat::zeros(LIDAR_MAT_HIGH,LIDAR_MAT_WIDTH,CV_8UC3);
Mat  lidar_right_mat = Mat::zeros(LIDAR_MAT_HIGH,LIDAR_MAT_WIDTH,CV_8UC3);

int main(int argc, char** argv)
{
    #if defined(TEST_MODE) || defined (DEBUG_MODE)
    //init_trackbars();
    namedWindow("rect_trackbars");
    init_rect_trackbars("rect_trackbars");//trackbars for rect opt
    #endif

    //splines
    spline_t left_lidar_spline;
    spline_t right_lidar_spline;
    spline_t trajectory_path_spline;

    //set default spline = go straigth
    vector<Point> straight_vector;
    straight_vector.push_back(Point(LIDAR_MAT_WIDTH/2,LIDAR_MAT_HEIGHT));
    straight_vector.push_back(Point(LIDAR_MAT_WIDTH/2,LIDAR_MAT_HEIGHT/2));
    straight_vector.push_back(Point(LIDAR_MAT_WIDTH/2,0));

    left_lidar_spline.set_spline(straight_vector);
    right_lidar_spline.set_spline(straight_vector);
    trajectory_path_spline.set_spline(straight_vector);

    //tangents
    tangent tangents[5];
    tangent middle_tangent;
    tangent trajectory_tangent;

    //sharedmemory
    SharedMemory left_lidar_points_shm(50001);
    vector<Point>left_lidar_vec;
    left_lidar_points_shm.init(); //init

    SharedMemory right_lidar_points_shm(50002);
    vector<Point>right_lidar_vec;
    right_lidar_points_shm.init(); //init

    SharedMemory additional_data_shm(50003,32);
    vector<uint32_t> add_data_container(7);
    additional_data_shm.init();

    //usb communication
    USB_STM USB_COM;
    data_container to_send;
    uint32_t velocity_to_send;
    uint32_t angle;

    USB_COM.init();//init

    init_trackbars();

    bool left_line_detect = 0;
    bool right_line_detect = 0;

    //average angle
    uint32_t average_angle_counter = 0;
    uint32_t angle_sum = 0;

    uint32_t angle_to_send;
    uint32_t velocity;

    //offsets and wagis
    float ang_div = 0;
    int drag_offset = 300; //default
    float servo_weight = 0.5;
    float far_tg_fac = 0.9;

////////////////////////////////////////////WHILE///////////////////////////////////////////////////
while(1)
{
    #if defined(TEST_MODE) || defined(DEBUG_MODE)
        number_of_rec_cols = rect_slider[0];
        number_of_rec_raws = rect_slider[1];
    #endif


    //clear buffer which could have another size
    left_lidar_vec.clear();
    right_lidar_vec.clear();

    #ifdef DEBUG_MODE

    test_mat = Mat::zeros(LIDAR_MAT_HIGH, LIDAR_MAT_WIDTH, CV_8UC3 );
    output_mat = Mat::zeros(LIDAR_MAT_HIGH, LIDAR_MAT_WIDTH, CV_8UC3 );

    lidar_left_mat = Mat::zeros(LIDAR_MAT_HIGH,LIDAR_MAT_WIDTH,CV_8UC3);
    lidar_right_mat = Mat::zeros(LIDAR_MAT_HIGH,LIDAR_MAT_WIDTH,CV_8UC3);


    //get new set of points
    right_lidar_points_shm.pull_lidar_data(right_lidar_vec);
    left_lidar_points_shm.pull_lidar_data(left_lidar_vec);

    //get additional data
    additional_data_shm.pull_add_data(add_data_container);

    //preview of received points
    points_preview(right_lidar_vec,test_mat,CV_RGB(255,255,255));
    points_preview(left_lidar_vec,test_mat,CV_RGB(255,255,0));

    #endif

    //detection flags
    left_line_detect = 0;
    right_line_detect = 0;

    //begin y condition
    if(left_lidar_vec.size()>2)
    {
        new_optimization(left_lidar_vec,left_lidar_spline,lidar_left_mat);
        left_line_detect = 1;
    }
    if(right_lidar_vec.size()>1)
    {
        new_optimization(right_lidar_vec,right_lidar_spline,lidar_right_mat);
        right_line_detect = 1;        
    }

    //set path according to lines
    if(left_line_detect == 1 && right_line_detect == 1)
    {
       two_line_planner(left_lidar_spline,right_lidar_spline,0,trajectory_path_spline);
       trajectory_tangent.calculate(trajectory_path_spline,rect_slider[3]);
       trajectory_tangent.angle();

       middle_tangent.calculate(trajectory_path_spline,200);
       middle_tangent.angle();
    }
    else if(right_line_detect)
    {
         one_line_planner(right_lidar_spline,-90,trajectory_path_spline);
         trajectory_tangent.calculate(trajectory_path_spline,rect_slider[3]);
         trajectory_tangent.angle();

         middle_tangent.calculate(trajectory_path_spline,200);
         middle_tangent.angle();
    }
    else if(left_line_detect)
    {
        one_line_planner(left_lidar_spline,-90,trajectory_path_spline);
        trajectory_tangent.calculate(trajectory_path_spline,rect_slider[3]);
        trajectory_tangent.angle();

        middle_tangent.calculate(trajectory_path_spline,200);
        middle_tangent.angle();
    }

///////////////////////////////////////////////////////////////////////////////////////////
     #if defined(RACE_MODE) || defined(DEBUG_MODE)

        velocity = 2000;
        servo_weight = 1.3;
        ang_div = abs(middle_tangent.angle_deg - trajectory_tangent.angle_deg);
        far_tg_fac = 0.7;

        if(abs(trajectory_tangent.angle_deg)<20)
            velocity_to_send =8000;
        else
            velocity_to_send = 5000;

        //angle = 300 + servo_weight*((far_tg_fac*middle_tangent.angle_deg + (1.0-far_tg_fac)*trajectory_tangent.angle_deg))*10;
        //5 tangents
        angle = 300 + servo_weight*(0.3*trajectory_tangent.angle_deg + 0.2*tangents[0].angle_deg +  0.2*tangents[1].angle_deg + 0.2*tangents[2].angle_deg + 0.2*tangents[3].angle_deg + 0.2*tangents[4].angle_deg);

        angle_to_send = angle;
        //send data to STM
        //USB_COM.data_pack(velocity_to_send,angle_to_send,usb_from_vision,&to_send);

        //USB_COM.send_buf(to_send);

        //read 12 byte data from stm 0-2 frame, 3-6 velocity, 7-8 tf_mini, 9-10 futaba gears

        //USB_COM.read_buf(14,car_velocity,tf_mini_distance,taranis_3_pos,taranis_reset_gear,stm_reset,lights);

        //app reset
        //if(taranis_reset_gear)
            //system("gnome-terminal --geometry 20x35+0+0 -x sh -c '~/Desktop/Selfie-autonomous-car/DRAG/DRAG.sh; bash'");


       //cout<<"3 pos: "<<int(taranis_3_pos)<<" reset "<<int(taranis_reset_gear)<<" stm_reset "<<(int)stm_reset<<" ligths "<<(int)lights<<endl;


        //read data from STM
        angle_sum = 0;
        average_angle_counter = 0;

    #endif

////////////////////////////////////////////////////////////////////////////////////////////////////////
     //display section
    string label;
    #ifdef DEBUG_MODE

        add(lidar_right_mat,lidar_left_mat,output_mat); // to see rectangles

        //draw detected spline
        if(right_line_detect)
            right_lidar_spline.draw(output_mat,CV_RGB(255,255,0));
        if(left_line_detect)
            left_lidar_spline.draw(output_mat,CV_RGB(255,255,255));

        trajectory_path_spline.draw(output_mat,CV_RGB(0,255,0));
        trajectory_tangent.draw(output_mat,CV_RGB(100,100,100));
        middle_tangent.draw(output_mat,CV_RGB(255,0,255));


        label  = "traj_ang: "+ std::to_string(trajectory_tangent.angle_deg);

        putText(output_mat, label, Point(450, 40), FONT_HERSHEY_SIMPLEX, 0.5, CV_RGB(0,255,0), 1.0);

        label = std::to_string(middle_tangent.angle_deg);
        putText(output_mat, label, Point(450, 80), FONT_HERSHEY_SIMPLEX, 0.5, CV_RGB(0,255,0), 1.0);
        //show white line mat
        imshow("tryb DEBUG",output_mat);

        //clean matrixes

        if(waitKey(30)=='q')
            break;
    #endif
}//end of while(1)

   left_lidar_points_shm.close();
   right_lidar_points_shm.close();
   additional_data_shm.close();

    return 0;

}
