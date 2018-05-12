#include <cstdlib>
#include <thread>
#include <mutex>
#include <sstream>
#include <iostream>
#include <queue>
#include <vector>
#include <memory>
#include <algorithm>
#include <boost/asio.hpp>
#include <boost/exception/diagnostic_information.hpp> 
#include <png++/png.hpp>

using boost::asio::ip::tcp;

struct pixel{
	pixel(){};
	pixel(png::rgb_pixel px,
	      unsigned int dst_x,
	      unsigned int dst_y):X(dst_x),Y(dst_y),R(px.red),G(px.green),B(px.blue){};
	unsigned int X;
	unsigned int Y;
	unsigned int R;
	unsigned int G;
	unsigned int B;
	operator std::string(){
		std::stringstream s;
		s<<"PX "<<X<<" "<<Y<<" "<<std::hex<<std::setfill('0');
		s<<std::setw(2)<<R;
		s<<std::setw(2)<<G;
		s<<std::setw(2)<<B;
		s<<std::endl;
		return s.str();
	}
};

std::vector<std::string> pixels;
bool run=true;

struct worker{
	boost::asio::io_service &io;
	int start;
	int end;
	worker(const worker &o):io(o.io),start(o.start),end(o.end){};
	worker(boost::asio::io_service &IO,int Start, int End):io(IO),start(Start),end(End){} ;
	std::unique_ptr<tcp::socket> sock;
	void operator ()(){
		std::cerr<<"thread\n";
		tcp::resolver resolver(io);
		std::string host{std::getenv("PIXELHOST")};
		std::cout<<"Host: "<<host<<std::endl;
		tcp::resolver::query q(host,"1234");
		auto endpoint=*resolver.resolve(q);
		while(run){
			sock.reset(new tcp::socket(io));
			try{
				sock->connect(endpoint);
			}
			catch (boost::system::system_error &e){
				continue;
			}
			std::cerr<<"connecting\n";
			bool inner=true;
			while(inner){
				if (!run) break;
				for(int p=start;p<end;p++){
					if(!(run && inner)) break;
					try{
						sock->send(boost::asio::buffer(pixels[p]));
					}
					catch (boost::system::system_error &e){
						//std::cout<<e.what()<<std::endl<<boost::diagnostic_information(e);
						std::this_thread::yield();
						inner=false;
					}
				}
			}
		}
	}
};

int main(int argc, char* argv[]){
	int THREADS;
	std::stringstream s;
	s<<argv[1]<<" "<<argv[2]<<" "<<argv[4]<<" "<<argv[5]<<" "<<argv[6];
	int X0;
	int Y0;
	int Xmin,Xmax;
	s>>X0;
	s>>Y0;
	s>>THREADS;
	s>>Xmin;
	s>>Xmax;
	std::cout<<"X:"<<X0<<" Y:"<<Y0<<std::endl;

	png::image< png::rgb_pixel > image(argv[3]);
	const auto w=image.get_width();
	const auto h=image.get_height();
	boost::asio::io_service io;

	for(int x=Xmin;x<(w<Xmax?w:Xmax);x++){
		for(int y=0;y<h;y++){
			auto px=pixel(image[x][y],X0+x,Y0+y);
			pixels.emplace_back(px);
		}
	}
	std::random_shuffle(begin(pixels),end(pixels));
	std::vector<worker> workers;
	std::vector<std::thread> threads;
	int stride=pixels.size()/THREADS;
	for (int i=0;i<THREADS;i++){
		workers.emplace_back(io,i*stride,(i+1)*stride);
	}
	for(auto worker:workers){
		threads.emplace_back(worker);
	}
	try{
		while(1){
			/*
			for (const auto &d:pixels){
					{
						std::size_t size=0;
						while (size>100){
							{
								std::lock_guard<std::mutex> g(m);
								size=q.size();
							}
							std::this_thread::yield();
						}
						std::lock_guard<std::mutex> g(m);
						q.push(d);
					}
			}
		*/
			std::this_thread::sleep_for(std::chrono::seconds(1));
		}
	}
	catch (boost::system::system_error &e){
		std::cout<<e.what()<<std::endl<<boost::diagnostic_information(e);
	}
	return 0;
}
