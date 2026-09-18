#include "common.hpp"
#include <opencv2/opencv.hpp>
static void mouse(int event,int x,int y,int,void*user){if(event!=cv::EVENT_LBUTTONDOWN)return;auto&v=*static_cast<std::vector<Point>*>(user);if(v.size()<3)v.push_back({double(x),double(y)});else v[2]={double(x),double(y)};}
int main(int argc,char**argv){try{
 Args a(argc,argv,{"--video","--servo","--seq","--frame","--out"});int servo=std::stoi(a.need("--servo")),seq=std::stoi(a.need("--seq"));require(servo>=1&&servo<=4&&seq>=0,"Invalid servo/sequence");
 fs::path path=fs::absolute(a.need("--video"));auto hash=sha256(path);cv::VideoCapture cap(path.string());require(cap.isOpened(),"Cannot open video");int total=int(cap.get(cv::CAP_PROP_FRAME_COUNT));require(total>0,"Frame count unavailable");double fps=cap.get(cv::CAP_PROP_FPS);int frame=std::clamp(std::stoi(a.get("--frame","0")),0,total-1);
 fs::path out=a.get("--out","measurements.csv");const std::string header="video,sha256,servo,command_seq_manual,frame,reported_pts_ms,fps_metadata,nominal_frame_time_s,pivot_x,pivot_y,zero_x,zero_y,tip_x,tip_y,angle_deg,source";
 bool fresh=!fs::exists(out)||fs::file_size(out)==0;if(!fresh){std::ifstream existing(out.string());std::string first;std::getline(existing,first);if(!first.empty()&&first.back()=='\r')first.pop_back();require(first==header,"CSV schema mismatch; choose a new output");}
 std::ofstream log(out.string(),std::ios::app);log.exceptions(std::ios::badbit|std::ios::failbit);log.precision(12);if(fresh)log<<header<<'\n';
 std::vector<Point> points;cv::namedWindow("measure",cv::WINDOW_AUTOSIZE);cv::setMouseCallback("measure",mouse,&points);std::signal(SIGINT,on_signal);
 cv::Mat original;int loaded=-1;double pts=0;
 while(!interrupted){if(frame!=loaded){require(cap.set(cv::CAP_PROP_POS_FRAMES,frame)&&cap.read(original),"Frame seek/read failed");loaded=frame;pts=cap.get(cv::CAP_PROP_POS_MSEC);}cv::Mat img=original.clone();
 for(size_t i=0;i<points.size();i++){cv::Point pt(int(points[i].x),int(points[i].y));cv::circle(img,pt,4,{0,255,0},-1);cv::putText(img,std::to_string(i),pt,0,.6,{0,255,0},1);}
 cv::putText(img,"Click pivot, ZERO direction, tip. A/D +/-1 J/L +/-10 S save R reset Q quit",{8,22},0,.45,{0,255,255},1);
 cv::putText(img,"frame="+std::to_string(frame)+" servo="+std::to_string(servo)+" seq="+std::to_string(seq),{8,45},0,.6,{0,255,255},1);cv::imshow("measure",img);
 int k=cv::waitKey(30)&255;if(k=='q'||k==27||cv::getWindowProperty("measure",cv::WND_PROP_VISIBLE)<1)break;if(k=='r')points.clear();
 if(k=='a'||k=='d'||k=='j'||k=='l'){frame=std::clamp(frame+(k=='a'?-1:k=='d'?1:k=='j'?-10:10),0,total-1);if(points.size()==3)points.pop_back();}
 if(k=='s'&&points.size()==3){double deg;try{deg=angle(points[0],points[1],points[2]);}catch(const std::exception&e){std::cerr<<e.what()<<'\n';continue;}
 log<<csv_quote(path.string())<<','<<hash<<','<<servo<<','<<seq<<','<<frame<<','<<pts<<','<<fps<<',';if(fps>0)log<<frame/fps;for(auto&p:points)log<<','<<p.x<<','<<p.y;log<<','<<deg<<",manual_video_planar\n";log.flush();std::cout<<"Saved frame="<<frame<<" angle_deg="<<deg<<std::endl;}}
 cap.release();cv::destroyAllWindows();return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
