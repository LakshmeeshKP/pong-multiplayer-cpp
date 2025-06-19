// for compiling in terminal: g++ PongGame.cpp -o PongGame.exe -lws2_32
// if you are compiling in vscode, uncomment #pragma comment(...) to link the ws2_32.lib library

#include <iostream>
#include <thread> // for multi-threading
#include <mutex> // for synchronizing the threads
#include <condition_variable> //use to pause all threads at a condition
#include <chrono> // for delays
#include <conio.h> // for getting keyboard inputs
#include <winsock2.h> //for windows sockets
#include <ws2tcpip.h> //for windows sockets
#include <random> // to generate random angles for ball motion
#include <cmath> // for sin, cos operations for ball motion
#include <array>
#include <vector>

//#pragma comment(lib, "ws2_32.lib")

using namespace std;

SOCKET clientSocket; //store client socket info.
char input;  // '1' => left side & server ; '2' => right side & client;

int gameStarted=false;

//dimensions of playing area
int rows=10;
int cols=30;

//for rendering and ball motion
int refresh_time = 50; //in milliseconds

//player paddle positions
int y=0; //stores paddle position
int l=0; //left paddle position
int r=0; //right paddle position

//ball position
int ball_x=cols/2;
int ball_y=rows/2;
float ball_xfp=cols/2;
float ball_yfp=rows/2;
int angle;

//random number generator
random_device rd;
mt19937 gen(rd());
uniform_int_distribution<> initial_dir(0, 359); //initial ball direction
uniform_int_distribution<> left_bounce(290,430); //new direction on bouncing from left paddle
uniform_int_distribution<> right_bounce(120, 250); //new direction on bouncing from right paddle

//for thread sync
mutex mtx;
condition_variable cond;
bool paused = false; //to pause game after a score

array<int,2> score_arr={0,0};  //stores player scores

//function declarations
void render();
void ball_motion();
void score(int side);
void update();
void initSocket();
void rec_msg();
void update_testing();
void send_msg();

//////communication functions starts///////

// reference for sockets: https://learn.microsoft.com/en-us/windows/win32/api/winsock2/

//initialize socket connection
void initSocket(){
    WSADATA wsaData;
    //struct which stores details of the windows socket

    WSAStartup(MAKEWORD(2,2), &wsaData);
    //MAKEWORD makes a 16 bit word out of byte inputs (0x0202 ie. version 2.2)
    //syntax: WSAStartup( input version no. of windows sockets , output wsadata struct which stores version info. & implementation info.)

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    //creates a socket
    //syntax: socket(input Address family, input socket connection type, input protocol)

    sockaddr_in serverAddr{};
    //struct which stores the address info. of the server,
    //{} inits all the fields of the struct to 0

    serverAddr.sin_family = AF_INET;
    //set server IP address family
    //AF_INET for IPv4 address family, AF_INET6 for IPv6, AF_UNIX for Unix domain sockets

    serverAddr.sin_port = htons(8080);
    //set server port address; make sure firewall allows communication through this port at both sides
    //htons(host to network short) : converts the port from your computer’s native byte order (little endian) to the network byte order (big endian)


    //goto label defined to repeat player connection process
    label1:
    cout<<"Enter '1' to Accept request, '2' to Send request:"<<endl;
    cin>>input;
    if (input=='1'){  //server

        //Code segment to get self local IP addres
        char hostname[256];
        if (gethostname(hostname, sizeof(hostname)) == SOCKET_ERROR) {  //stores host computers name into hostname
            cout<<"gethostname() failed\n";
            WSACleanup();
            while(true){}
        }

        hostent *host = gethostbyname(hostname);
        //converts the hostname into IP address
        //hostent struct stores info. about a host such as hostname, IP addresses, etc.
        if (host == nullptr) {
            cout<< "gethostbyname() failed\n";
            WSACleanup();
            while(true){}
        }

        vector<string> valid_ip;
        for (int i = 0; host->h_addr_list[i]; i++) {  //iterate through all addresses
            memcpy(&serverAddr.sin_addr, host->h_addr_list[i], host->h_length); //copy IP address into serverAddr struct
            char* ip = inet_ntoa(serverAddr.sin_addr); //converts binary IP to standard dotted decimal format
            if (strcmp(ip, "127.0.0.1") != 0) // skip loopback mode IP address
                valid_ip.push_back(ip);

        }
        if (valid_ip.size()>1){
            cout<<"copy any IP on the opponent PC:"<<endl;
            for (int i=0; i<valid_ip.size(); i++){
                cout<<valid_ip[i]<<endl;
            }
        }
        else{
            cout<<"copy this IP on the opponent PC:"<<valid_ip[0]<<endl;
        }


        // code segment for establishing connection

        serverAddr.sin_addr.s_addr = INADDR_ANY;  //INADDR_ANY : listen for any IP

        bind(serverSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr));
        // associates local address with a socket
        // syntax: bind(input socket, input pointer to socket address struct, input length of struct pointed to)

        listen(serverSocket, 3);
        //server socket starts listening for incoming connections
        // syntax: listen(socket name , backlog - length of queue for pending connections)

        cout<<".......waiting for client to connect......."<<endl;

        clientSocket = accept(serverSocket, nullptr, nullptr);
        // accepts connection from any socket which tries to connect to the listening server socket
        // syntax: accept(input unconnected socket in listening state, output ptr address of connected socket, output ptr length of the addr struct)

        if (clientSocket==INVALID_SOCKET) {
            cout<<"ERROR in accept()"<<endl;
            while(true){}
            }
        else cout<<".....Client connected......"<<endl;

    }
    else if (input=='2'){  //client
        // sending connection request

        // get client IP address
        string IPv4_server;
        cout<<"enter the opponents IP address: ";

        cin.ignore(numeric_limits<streamsize>::max(), '\n');  //ignores any leftover newline character in input buffer for getline()
        getline(cin,IPv4_server);
        serverAddr.sin_addr.s_addr = inet_addr(IPv4_server.c_str()); //convert const char to proper address for address struct

        clientSocket=serverSocket;
        //define client socket to be the connected server socket
        //redefining needed since both client and server share same code for receiving and sending

        auto connect_ret = connect(clientSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr));
        //connect(input socket which I want to connect to, input socket addr ptr, input sockaddr struct len);

        if ( connect_ret == 0){
            cout<<".......connected to server......."<<endl;
        }
        else {
            cout<<"ERROR in connect()"<<endl;
            while(true){}
        }

    }
    else{
        cout<<"invalid input"<<endl;
        goto label1;
    }
    gameStarted=true; //start other threads execution
}

// to send data to opponent
void send_msg(){
       while(gameStarted){
            //pause thread when someone scores
            unique_lock<mutex> lock(mtx);
            cond.wait(lock, [] { return !paused; });
            lock.unlock();

            if (input=='1') { //server
                int msg[3]={y,ball_x,ball_y};
                send(clientSocket, (char*)msg, 3*sizeof(int), 0); // paddle position + ball position sent
            }
            else{ //client
                send(clientSocket, (char*)&y, sizeof(int), 0); // only paddle position sent
            }
            this_thread::sleep_for(chrono::milliseconds(refresh_time));
       }
    }

// to receive data from opponent
void rec_msg(){
    while (gameStarted){
        //pause thread when someone scores
        unique_lock<mutex> lock(mtx);
        cond.wait(lock, [] { return !paused; }); //blocks thread until true is returned
        lock.unlock();

        if (input=='1'){ //server
            int msg;

            //recv(input socket connected ext socket, char* out received data buffer, input len of buffer (in bytes), input
            if (recv(clientSocket, (char*)&msg, sizeof(int), 0)>0){ //receives opponent paddle position
                r=msg;
            }
            else{
                // pause all threads when socket connection ends
                {
                    unique_lock<mutex> lock(mtx);
                    paused=true;  //stop all other threads
                }
                cond.notify_all();

                this_thread::sleep_for(std::chrono::milliseconds(50));  //small delay to ensure all threads have halted
                system("cls");

                cout<<"CONNECTION ENDED"<<endl;
                while(true){}
            }
        }
        else {  //client
            int msg[3];
            if (recv(clientSocket, (char*)msg, 3*sizeof(int), 0)>0){  //receives {opponent paddle position, ball_x, ball_y}
                l=msg[0];
                ball_x=msg[1];
                ball_y=msg[2];
            }
            else{
                // recv() returns 0 or SOCKET_ERROR if socket connection breaks

                //pauses all other threads when socket connection ends
                {
                    unique_lock<mutex> lock(mtx);
                    paused=true;  //stop all other threads
                }
                cond.notify_all();

                this_thread::sleep_for(std::chrono::milliseconds(50)); //small delay to ensure all threads have halted
                system("cls");

                cout<<"CONNECTION ENDED"<<endl;
                while(true){}
            }
        }
        this_thread::sleep_for(std::chrono::milliseconds(refresh_time));
    }
}

/////////communication functions ends//////////

////////////game related functions starts////////

//render game state on terminal
void render(){
    system("cls"); //initial terminal screen clear
    while(true){
        //pause thread at score
        unique_lock<mutex> lock(mtx);
        cond.wait(lock, [] { return !paused; }); //blocks thread until true is returned
        lock.unlock();

        //for rendering
        for (int i=0; i<rows; i++){
            for (int j=0; j<cols; j++){
                if (j==0 || j==cols-1) cout<<"|"; //print boundary
                else if (i==l && j==2) cout<<")"; //print left paddle
                else if (i==r && j==cols-3) cout<<"("; //print right paddle
                else if (i==ball_y && j==ball_x) cout<<"o"; //print ball
                else cout<<" "; //print empty space
            }
            cout<<endl;
        }
        cout << "\033[0;0H"; // move cursor to (0,0) and start over-writing  //ANSI escape code: \033[<ROW>;<COL>H
        this_thread::sleep_for(std::chrono::milliseconds(refresh_time)); //wait for refresh time
    }
}

//for ball motion
void ball_motion(){
    //the ball state is calculated at server side
    //the client replicates the image from the server
    if (input=='1'){
        int speed=30; //speed of ball
        angle=initial_dir(gen); //initial angle of motion
        while (angle==90 || angle==270) angle=initial_dir(gen); //ensures ball doesn't start perpendicularly

        //after connection is made and game starts
        while (gameStarted){

            //pause thread when someone scores
            unique_lock<mutex> lock(mtx);
            cond.wait(lock, [] { return !paused; }); //blocks thread until true is returned
            lock.unlock();

            //calculate position in floating point for precision
            ball_xfp+=(speed*cos(angle*3.14/180)*refresh_time/1000);
            ball_yfp+=(speed*sin(angle*3.14/180)*refresh_time/1000);

            //convert to int for display
            ball_x=round(ball_xfp);
            ball_y=round(ball_yfp);

            //bounce from left paddle
            if ( (ball_x==3 ||ball_x==2) && (ball_y<=l+1 && ball_y>=l-1)){// && (angle>90 && angle<270)){
                ball_x=3;
                ball_xfp=3;
                angle = (left_bounce(gen))%360; //get new angle after bouncing
            }
            //bounce from right player
            else if ( (ball_x==cols-4||ball_x==cols-3) && (ball_y<=r+1 && ball_y>=r-1) ){// && (angle<90 || angle>270)){
                ball_x=cols-4;
                ball_xfp=cols-4;
                angle = right_bounce(gen); //get new angle after bouncing
            }
            //left player scores
            else if (ball_x>cols-1) {
                score(0);
            }
            //right player scores
            else if (ball_x<0) {
                score(1);
            }

            //bounce from top/bottom of screen
            if (ball_y>rows-1){
                ball_y=rows-1;
                ball_yfp=rows-1;
                angle=360-angle;
            }
            else if (ball_y<0){
                ball_y=0;
                ball_yfp=0;
                angle=360-angle;
            }

            //add delay
            this_thread::sleep_for(std::chrono::milliseconds(10*refresh_time));
        }
    }
    //client part of script
    else{
        while (gameStarted){
            //pause thread when someone scores
            unique_lock<mutex> lock(mtx);
            cond.wait(lock, [] { return !paused; });  //blocks thread until true is returned
            lock.unlock();

            //scoring script
            if (ball_x>cols-1) {
                score(0);
            }
            else if (ball_x<0) {
                score(1);
            }

            //add delay
            this_thread::sleep_for(std::chrono::milliseconds(10*refresh_time));
        }
    }
}

void score(int side){  //0-left scored 1-right scored

    //pauses all other threads
    {
        unique_lock<mutex> lock(mtx);
        paused=true;  //stop all other threads
    }
    cond.notify_all();

    //small delay to ensure all other threads are halted
    this_thread::sleep_for(std::chrono::milliseconds(500));
    system("cls");

    //update scores
    score_arr[side]++;

    //display score message for 3 seconds
    cout<<(side? "left":"right")<<" scored!\n\n";
    cout<<"left: "<<score_arr[0]<<"  right: "<<score_arr[1];
    this_thread::sleep_for(std::chrono::milliseconds(3000));
    system("cls");

    //reset ball and paddle positions
    ball_xfp=cols/2;
    ball_yfp=rows/2;
    ball_x=cols/2;
    ball_y=rows/2;
    y=0;
    l=0;
    r=0;

    //generate initial ball angle
    if (input==1){
        angle=initial_dir(gen);
        while (angle==90 || angle==270) angle=initial_dir(gen);
    }

    //allow other threads to start running
    {
        unique_lock<mutex> lock(mtx);
        paused=false;
    }
    cond.notify_all();
}

// for keyboard inputs to move paddle
void player_input(){
    while(true){
        int key; //store input key

        //for pausing after a score
        unique_lock<mutex> lock(mtx);
        cond.wait(lock, [] { return !paused; }); //blocks thread until true is returned
        lock.unlock();

        while(!kbhit()){} //wait until any key is pressed
        key=_getch(); //get pressed key

        //arrow keys are part of extended keys
        //extended keys are received as two characters
        //(others include function keys, delete, home, end etc.)

        if (key==0 || key==224){ //extended key detected
            key=_getch(); //get second part of extended key
            if (key==72){  //up arrow
                if (y>0) y--; //lower bound of paddle position
            }
            else if(key==80){  //down arrow
                if (y<9) y++; //upper bound of paddle position
            }

            //update left/right paddle position
            if (input=='1') {
                l=y;
            }
            else{
                r=y;
            }
        }
    }
}
////////////game related functions ends////////





int main(){
    system("cls"); //clear screen

    cout << "\033[?25l";  //hide the cursor using ANSI escape code

    initSocket();  //initialize socket connections

    //threads initialization
    thread t1(render);        //display game play
    thread t2(player_input);  //get player input
    thread t3(rec_msg);       //receive message from other PC
    thread t4(send_msg);      //send message to other PC
    thread t5(ball_motion);   //to move ball

    t1.join();
    t2.join();
    t3.join();
    t4.join();
    t5.join();
}