/******************************
 Author: Daniel Alner & Andrew Alter
  --21CRobot API
  --Include wrapper around 21CRobot for Javascript API, can include other API's
*****************************/

#include <unistd.h>
#include <string>
#include <libgen.h>
#include <ncurses.h>
#include <term.h>
#include <termios.h>
#include <stdint.h>

#include "Camera.h"
#include "Point.h"
#include "mjpg_streamer.h"
#include "minIni.h"
#include "LinuxCamera.h"
#include "ColorFinder.h"

#include "Action.h"
#include "Head.h"
#include "Walking.h"
#include "MX28.h"
#include "MotionManager.h"
#include "LinuxMotionTimer.h"
#include "LinuxCM730.h"
#include "LinuxActionScript.h"

// defines config file path which contain walk tuning params,
// currently generated by walk_tuner
#define INI_FILE_PATH       "../../../Data/config.ini"
// define motion binary file path which contains the action page data
#define MOTION_FILE_PATH    "../../../Data/motion_4096.bin"

// used to check which dev assignment the subcontroller is on
#define U2D_DEV_NAME0       "/dev/ttyUSB0"
#define U2D_DEV_NAME1       "/dev/ttyUSB1"

// instatiate subcontroller object with dev assignment
LinuxCM730 linux_cm730(U2D_DEV_NAME0);
CM730 cm730(&linux_cm730);
// instatiate system timer
LinuxMotionTimer linuxMotionTimer;

// TODO(Anyone) used to manage current positional state
Action::PAGE Page;
Action::STEP Step;

/**************************************
***      Forward Declaration         **
****************************************/
int PlayAction(int pageNumber);
void WalkToggle(bool onOff);
void WalkControl(int x, int y);
void ServoStartup();
void ServoShutdown();

/**************************************
***                  General         **
****************************************/

// Initializes the servos, gets the motion path
// gets access to Motion Manger etc.
bool Initialize() {
    minIni* ini = new minIni(INI_FILE_PATH);

    // check if on dev 0
    // Motion manager initialize queries all servos
    if (MotionManager::GetInstance()->Initialize(&cm730) == false) {
        // if not try dev 1
        linux_cm730.SetPortName(U2D_DEV_NAME1);
        if (MotionManager::GetInstance()->Initialize(&cm730) == false) {
            printf("\n The Framework has encountered a connection error to the subcontroller!\n");
            printf("Failed to initialize Motion Manager! Meaning the program is unable to communicate with Subcontroller.\n");            
            printf("Please check if subcontroller serial port is recognized in /dev/ as TTYUSB0 or TTYUSB1, or if port is currently in use by another program.\n");  
            return false;
         }
    }

    // load config ini for walking gait params
    Walking::GetInstance()->LoadINISettings(ini);
    usleep(100);
    // load config ini offset values Maybe to comment out if not used
    MotionManager::GetInstance()->LoadINISettings(ini);

    // instantiates each motion module to motion manager
    MotionManager::GetInstance()->AddModule((MotionModule*)Action::GetInstance());
//    MotionManager::GetInstance()->AddModule((MotionModule*)Head::GetInstance());
    MotionManager::GetInstance()->AddModule((MotionModule*)Walking::GetInstance());
    
    Walking::GetInstance()->m_Joint.SetEnableBody(false);
    Action::GetInstance()->m_Joint.SetEnableBody(true);
    // instatiate sys timer and start
    linuxMotionTimer.Initialize(MotionManager::GetInstance());
    linuxMotionTimer.Start();

    // load motion binary to action module
    Action::GetInstance()->LoadFile(MOTION_FILE_PATH);

    return true;
}

// turn all servos off (make sure robot is sitting)
void ServoShutdown() {
    WalkToggle(false);  // turn off walking if is walking
    PlayAction(15);  // sit robot
    // shutdown
    cm730.DXLPowerOn(false);
}

void ServoStartup() {
    cm730.DXLPowerOn(true);
}

/**************************************
***                  Actions         **
****************************************/
// call page numbers
int PlayAction(int pageNumber) {
    // if servos are off then turn on
    ServoStartup();
    // stop walking motion
    WalkToggle(false);

    // enable all servos exclusively for action
    Action::GetInstance()->m_Joint.SetEnableBody(true, true);

    // need to verify standing robot but infer based on robot stance not page number
    //if(pageNumber != 4)
    //    Action::GetInstance()->Start(2);  // stand the robot before playing action unless its sit
    // maybe here we test to see if it is in standing motion already
    // while action is playing, wait before returning function
    while (Action::GetInstance()->IsRunning() == true) usleep(500);

    Action::GetInstance()->Start(pageNumber);
    // while action is playing, wait before returning function
    while (Action::GetInstance()->IsRunning() == true) usleep(500);

    //action completed
    return 0;
}

// call page number by name
// TODO(anyone)
/*
void PlayAction(std::string name) {
}*/

// turn on/off motors
// TODO(anyone)
void ServoPower(Robot::CM730 *cm730, bool on, int num_param,
                            int *list, char lists[30][10]) {
}

// set page numbers
void NewAction(std::string name) {
}

// get page number associated with name of action
int GetPageNumber(std::string name) {
    return -1;
}


/**************************************
***                   Walking        **
****************************************/
// turn walking on/off
void WalkToggle(bool onOff) {
    // if servos are off then turn on
    ServoStartup();

    if (onOff) {
        // turn on walk
        PlayAction(8);  // walking stance
        // enable walking minus head
        Walking::GetInstance()->m_Joint.SetEnableBodyWithoutHead(true, true);
        MotionManager::GetInstance()->SetEnable(true);
        // enable head motion module
        Head::GetInstance()->m_Joint.SetEnableHeadOnly(true);
        // initialze motion to 0 and start walking
        Walking::GetInstance()->X_MOVE_AMPLITUDE = 0;
        Walking::GetInstance()->Y_MOVE_AMPLITUDE = 0;
        Walking::GetInstance()->A_MOVE_AMPLITUDE = 0;
        Walking::GetInstance()->Start();
    } else {
        // turn off walk
        Walking::GetInstance()->Stop();
        while (Walking::GetInstance()->IsRunning() == 1) usleep(8000);
        MotionManager::GetInstance()->Reinitialize();
        // enable all servos for motion manager
        MotionManager::GetInstance()->SetEnable(true);
        Walking::GetInstance()->m_Joint.SetEnableBody(false);
        Action::GetInstance()->m_Joint.SetEnableBody(true);
    }
}

// location walking
void WalkControl(int x, int y) {
    // dead band might be able to be removed, this was used for ps3 remote
    // and because it was analog it did not always return 0, as ours is digital
    // we can and therefore to remove if testing goes well
    int dead_band = 6;
    double FBStep = 0, RLTurn = 0, RLStep = 0, xd, yd;
    static double speedAdjSum = 0;
    int x2 = -(x);
    int y2 = -(y);
    // used to ensure robot doesn't stop immediately and fall over when
    // joystick is reset back to the center
    if (abs(x2) > dead_band || abs(y2) > dead_band) {
        xd = (double)(x2-dead_band)/256;
        yd = (double)(y2-dead_band)/256;
        RLTurn = 60*xd;
        FBStep = 70*yd;  // 45
        if (FBStep < 0)
            FBStep = 45*yd;
            speedAdjSum += yd;
        if (speedAdjSum > Walking::GetInstance()->UPPER_VELADJ_LIMIT)
            speedAdjSum = Walking::GetInstance()->UPPER_VELADJ_LIMIT;
        else if (speedAdjSum < Walking::GetInstance()->LOWER_VELADJ_LIMIT)
            speedAdjSum = Walking::GetInstance()->LOWER_VELADJ_LIMIT;
    }

    // need to remove of this possbility
    speedAdjSum = 0;
    Walking::GetInstance()->speedAdj = speedAdjSum;
    Walking::GetInstance()->X_MOVE_AMPLITUDE = FBStep;
    Walking::GetInstance()->Y_MOVE_AMPLITUDE = RLStep;
    Walking::GetInstance()->A_MOVE_AMPLITUDE = RLTurn;
}

void WalkMove(double amount) {
    Walking::GetInstance()->X_MOVE_AMPLITUDE = amount;
}

void WalkTurn(double amount) {
    Walking::GetInstance()->A_MOVE_AMPLITUDE = amount;
}

// TODO(anyone) tune walking
void LoadINISettings(minIni* ini) {
}

/**************************************
***                   Head Motion    **
****************************************/
/*
// head pan tilt motion possible sample (still untested)
void MoveHeadByAngle(double pan, double tilt) {
    if((PS3BallFollower::GetInstance()->bHeadAuto == false && (m_cur_mode == SOCCER || m_cur_mode == SITTING)) \
        || (LineFollower::GetInstance()->bFullAuto == true && LineFollower::GetInstance()->bHeadAuto == false && m_cur_mode == LINE_FOLLOWING) \
        || (RobotFollower::GetInstance()->bFullAuto == true && RobotFollower::GetInstance()->bHeadAuto == false && m_cur_mode == ROBOT_FOLLOWING)) {
        int x, y, dead_band = 6;
        double pan, tilt;
        pan = MotionStatus::m_CurrentJoints.GetAngle(JointData::ID_HEAD_PAN);
        tilt = MotionStatus::m_CurrentJoints.GetAngle(JointData::ID_HEAD_TILT);
        Point2D pos = Point2D(pan, tilt);
        x = -(PS3.key.LJoyX-128);
        y = -(PS3.key.LJoyY-128);
        if (abs(x) > dead_band || abs(y) > dead_band) {
            pos.X = pan + 0.2*Camera::VIEW_V_ANGLE*(x-dead_band)/256;
            pos.Y = tilt + 0.2*Camera::VIEW_H_ANGLE*(y-dead_band)/256;
        }
        Head::GetInstance()->MoveByAngle(pos.X, pos.Y);
    }
}

*/
/**************************************
***                   Diagnostic     **
****************************************/
// check all servos and return failing servo or return value
int CheckServos() {
    return -1;
}

// check battery and return volt value or if below value, sit robot
// this returns a bit value from 0-255, 0=0Volts 11=11.1Volt 120=12.0 Volt etc.
// NOTE: current testing revealed this might not be the case even though doc's state
// might need to verify with some testing
int BatteryVoltLevel() {
    int voltage;

    if(cm730.ReadByte(CM730::ID_CM, CM730::P_VOLTAGE, &voltage, 0) == CM730::SUCCESS)
    {
        return voltage;
    }
    // failed, no read from cm730
    return -1;
}

// check servo heat value and return or sit if above value

// check wifi connectivity and values

// give alert of any other issues and return

/**************************************
***                   Sensors        **
****************************************/
// add sensor



/**************************************
***            Direct Motor Control  **
****************************************/
void SetMotorValue(int id, int value) {
	cm730.WriteWord(id, MX28::P_GOAL_POSITION_L, value, 0);
	// printf("Writing to id %d value %d\n", id, value);
	// Action::GetInstance()->m_Joint.SetValue(id, value);
	// MotionStatus::m_CurrentJoints.SetValue(id, value);
}

int GetMotorValue(int id) {
	int value;
	cm730.ReadWord(id, MX28::P_GOAL_POSITION_L, &value, 0);
	// printf("Value of id %d is %d\n", id, value);
	return value;
	// return Action::GetInstance()->m_Joint.GetValue(id);
	// return MotionStatus::m_CurrentJoints.GetValue(id);
}

// void SetMotorAngle(int id, double angle) {
	// Action::GetInstance()->m_Joint.SetAngle(id, angle);
	// MotionStatus::m_CurrentJoints.SetAngle(id, angle);
// }

// double GetMotorAngle(int id) {
	// return Action::GetInstance()->m_Joint.GetAngle(id);
	// return MotionStatus::m_CurrentJoints.GetAngle(id);
// }



/**************************************
***              External Refs       **
****************************************/
extern "C" {
    // General/Options
    bool InitializeJS() {return Initialize();}
    void ServoShutdownJS() {ServoShutdown();}
    void ServoStartupJS() {ServoStartup();}
    // Actions
    int PlayActionJS(int pageNumber) {return PlayAction(pageNumber);}
    // Walking
    void WalkJS(bool onOff) {WalkToggle(onOff);}
    void WalkingJS(int x, int y) {WalkControl(x, y);}
    void WalkMoveJS(double amount) {WalkMove(amount);}
    void WalkTurnJS(double amount) {WalkTurn(amount);}
    // Head Motion
    // void MoveHeadByAngleJS(double pan, double tilt)
    //                    {MoveHeadByAngle(pan, tilt);}
    // Diagnostic
    int CheckServosJS() {return CheckServos();}
    int BatteryVoltLevelJS() {return BatteryVoltLevel();}
    // Sensors
	// Motor Control
	void SetMotorValueJS(int id, int value) {SetMotorValue(id, value);}
	int GetMotorValueJS(int id) {return GetMotorValue(id);}
	// void SetMotorAngleJS(int id, double angle) {SetMotorAngle(id, angle);}
	// double GetMotorAngleJS(int id) {return GetMotorAngle(id);}
}