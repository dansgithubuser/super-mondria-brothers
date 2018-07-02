#include "sfml/system.hpp"
#include "sfml/graphics.hpp"
#include "sfml/audio.hpp"

#include "game.hpp"

#include "dansAudioLab.hpp"

#include <vector>
#include <sstream>

using namespace std;
using namespace dal;

const sf::Time MIN_FRAME_DURATION=sf::seconds(1.0f/FPS);

const unsigned SAMPLE_RATE=22050;
const unsigned CHANNELS=1;
const unsigned SAMPLES_AT_ONCE=1024;

class SoundStream: public sf::SoundStream{
	public:
		SoundStream(System* system): system(system)
			{ initialize(CHANNELS, SAMPLE_RATE); }
	private:
		//functions
		bool onGetData(Chunk& data){
			const float* samples=system->evaluate();
			data.sampleCount=SAMPLES_AT_ONCE;
			for(unsigned i=0; i<SAMPLES_AT_ONCE; ++i)
				int16samples[i]=sf::Int16(samples[i]*0x7ffd);
			data.samples=int16samples;
			return true;
		}

		void onSeek(sf::Time){}
		//variables
		sf::Int16 int16samples[SAMPLES_AT_ONCE];
		System* system;
};

void push(std::string s, int r, int c, vector<vector<pair<float, int> > >& result){
	result.resize(r);
	for(int i=0; i<r; ++i) result[i].resize(c);
	std::stringstream ss;
	ss<<s;
	for(int i=0; i<r; ++i)
		for(int j=0; j<c; ++j){
			float step;
			float dur;
			ss>>step;
			ss>>dur;
			result[i][j]=pair<float, int>(step/SAMPLE_RATE, SAMPLE_RATE*dur);
		}
}

System* createSystem(){
	//system
	System* system=new System(SAMPLE_RATE, SAMPLES_AT_ONCE);
	system->addComponent("adder", new Adder);
	system->attachToOutput(system->component("adder"));
	
	//sound effects
	vector<vector<pair<float, int> > > notes;
	
	push("450 0.125 550 0.125 500 0.125 600 0.125", 2, 2, notes);
	system->addComponent("playerJump", new Noter(notes));
	system->component("playerJump")>>system->component("adder");
	
	push("400 0.125 500 0.125", 1, 2, notes);
	system->addComponent("buddyJump", new Noter(notes));
	system->component("buddyJump")>>system->component("adder");
	
	push("200 0.125 150 0.125", 2, 1, notes);
	system->addComponent("playerBump", new Noter(notes));
	system->component("playerBump")>>system->component("adder");

	push("800 0.083 900 0.083 1000 0.083", 1, 3, notes);
	system->addComponent("powerup", new Noter(notes));
	system->component("powerup")>>system->component("adder");

	push(
		"300 0.03 375 0.03 "
		"300 0.03 375 0.03 "
		"300 0.03 375 0.03 "
		"300 0.03 375 0.03 "
		"270 0.03 337.5 0.03 "
		"270 0.03 337.5 0.03 "
		"270 0.03 337.5 0.03 "
		"270 0.03 337.5 0.03 "
		,
		2, 8, notes
	);
	system->addComponent("splash", new Noter(notes));
	system->component("splash")>>system->component("adder");

	return system;
}

int main(){
	//initialize
	sf::RenderWindow window(sf::VideoMode(640, 480), "LD26", sf::Style::Close);
	window.setKeyRepeatEnabled(false);
	sf::Clock clock;
	vector<Vertex> vertices;
	sf::VertexArray sfVertices;
	sfVertices.setPrimitiveType(sf::Quads);
	int maxFade=FPS*4;
	int fadeOut=maxFade;
	System* system=createSystem();
	Component* adder=&system->component("adder");
	SoundStream soundStream(system);
	Game game(system);
	sf::sleep(sf::seconds(0.1f));
	soundStream.play();
	//loop
	while(true){
		//handle events
		sf::Event sfEvent;
		while(window.pollEvent(sfEvent)){
			switch(sfEvent.type){
				case sf::Event::KeyPressed:
					switch(sfEvent.key.code){
						case sf::Keyboard::Space:
						case sf::Keyboard::W:
						case sf::Keyboard::Up:
							game.jumpPressed();
							break;
						case sf::Keyboard::A:
						case sf::Keyboard::Left:
							game.leftPressed();
							break;
						case sf::Keyboard::D:
						case sf::Keyboard::Right:
							game.rightPressed();
							break;
						default: break;
					}
					break;
				case sf::Event::KeyReleased:
					switch(sfEvent.key.code){
						case sf::Keyboard::Space:
						case sf::Keyboard::W:
						case sf::Keyboard::Up:
							game.jumpReleased();
							break;
						case sf::Keyboard::A:
						case sf::Keyboard::Left:
							game.leftReleased();
							break;
						case sf::Keyboard::D:
						case sf::Keyboard::Right:
							game.rightReleased();
							break;
						default: break;
					}
					break;
				case sf::Event::Closed:
					window.close();
					break;
				default: break;
			}
		}
		if(!window.isOpen()) break;
		if(fadeOut>=0){
			//update
			if(game.update()>FPS*4)
				if(fadeOut>0)
					--fadeOut;
			//draw
			vertices.clear();
			game.getQuadVertices(window.getSize().x, window.getSize().y, vertices);
			sfVertices.clear();
			for(unsigned i=0; i<vertices.size(); ++i)
				sfVertices.append(sf::Vertex(
					sf::Vector2f(
						vertices[i].x+window.getSize().x/2,
						-vertices[i].y+window.getSize().y/2
					),
					sf::Color(
						255*vertices[i].r*fadeOut/maxFade,
						255*vertices[i].g*fadeOut/maxFade,
						255*vertices[i].b*fadeOut/maxFade
					)
				));
			window.clear();
			window.draw(sfVertices);
			window.display();
		}
		if(fadeOut!=maxFade){
			float volume=1.0f*fadeOut/maxFade;
			adder->perform("volume", (void*)&volume);
		}
		//regulate
		sf::Time frameDuration=clock.restart();
		if(frameDuration<MIN_FRAME_DURATION)
			sf::sleep(MIN_FRAME_DURATION-frameDuration);
	}
	//finish
	soundStream.stop();
	delete system;
	return 0;
}
