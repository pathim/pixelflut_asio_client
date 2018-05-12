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

std::string create_px(unsigned int X,unsigned int Y,unsigned int R,unsigned int G,unsigned int B){
		std::stringstream s;
		s<<"PX "<<X<<" "<<Y<<" "<<std::hex<<std::setfill('0');
		s<<std::setw(2)<<R;
		s<<std::setw(2)<<G;
		s<<std::setw(2)<<B;
		s<<std::endl;
		return s.str();
}

struct pixel{
	pixel(){};
	pixel(png::rgb_pixel px,
	      unsigned int dst_x,
	      unsigned int dst_y):str{create_px(dst_x,dst_y,px.red,px.green,px.blue)}{};
	pixel(std::string s):str{s}{};
	std::string str;
	operator std::string(){
		return str;
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

void cluster_ctrl(char* argv[],int &X0, int &Y0, int &threads, int &Xmin, int &Xmax){
	std::stringstream s;
	s<<argv[1]<<" "<<argv[2]<<" "<<argv[4]<<" "<<argv[5]<<" "<<argv[6];
	s>>X0;
	s>>Y0;
	s>>threads;
	s>>Xmin;
	s>>Xmax;
	png::image< png::rgb_pixel > image(argv[3]);
	const auto w=image.get_width();
	const auto h=image.get_height();
	for(int x=Xmin;x<(w<Xmax?w:Xmax);x++){
		for(int y=0;y<h;y++){
			auto px=pixel(image[x][y],X0+x,Y0+y);
			pixels.emplace_back(px);
		}
	}
}
void cluster_client(char* argv[], int &threads, int &Xmin, int &Xmax, boost::asio::io_service &io){
	std::stringstream s;
	s<<argv[2];
	s>>threads;
	Xmin=0;
	Xmax=3000;

	std::string cluster_controller{argv[1]};
	tcp::resolver resolver(io);
	tcp::resolver::query q(cluster_controller,"1337");
	auto endpoint=*resolver.resolve(q);
	tcp::socket sock{io};
	sock.connect(endpoint);
	boost::asio::streambuf buf;
	auto n=boost::asio::read_until(sock,buf,'\n');
	buf.commit(n);
	std::istream is{&buf};
	std::string pixelhost;
	is>>pixelhost;
	setenv("PIXELHOST",pixelhost.c_str(),true);
	boost::system::error_code ec;
	while(ec!=boost::asio::error::eof){
		boost::asio::streambuf buf;
		auto n=boost::asio::read_until(sock,buf,'\n',ec);
		buf.commit(n);
		std::istream is{&buf};
		std::ostringstream ss;
		ss<<is.rdbuf();
		std::string pixeldata{ss.str()};
		std::cerr<<pixeldata<<std::endl;
		pixel px{pixeldata};
		pixels.emplace_back(px);
	}
}


int main(int argc, char* argv[]){
	int THREADS;
	int X0;
	int Y0;
	int Xmin,Xmax;
	boost::asio::io_service io;
	bool ctrl=false;
	if (argc==7){
		cluster_ctrl(argv,X0,Y0,THREADS,Xmin,Xmax);
		ctrl=true;
	}else if (argc==3){
		cluster_client(argv,THREADS,Xmin,Xmax,io);
	}else{
		return 1;
	}
	std::cout<<"X:"<<X0<<" Y:"<<Y0<<std::endl;

	std::random_shuffle(begin(pixels),end(pixels));
	std::vector<worker> workers;
	std::vector<std::thread> threads;
	std::cout<<pixels.size()<<std::endl;
	int stride=pixels.size()/THREADS;
	for (int i=0;i<THREADS;i++){
		workers.emplace_back(io,i*stride,(i+1)*stride);
	}
	for(auto worker:workers){
		threads.emplace_back(worker);
	}
	try{
		if (ctrl){
			std::string host{std::getenv("PIXELHOST")};
			std::stringstream msg;
			msg<<host<<"\n";
			for(auto p:pixels){
				msg<<p;
			}
			tcp::acceptor acceptor{io,tcp::endpoint{tcp::v4(),1337}};
			while(1){
				tcp::socket sock{io};
				acceptor.accept(sock);
				boost::system::error_code ignored_error;
				boost::asio::write(sock,boost::asio::buffer(msg.str()),ignored_error);
			}
		}else{
			while(1){
				std::this_thread::yield();
			}
		}
	}
	catch (boost::system::system_error &e){
		std::cout<<e.what()<<std::endl<<boost::diagnostic_information(e);
	}
	return 0;
}
