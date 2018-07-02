#ifndef GAME_HPP_INCLUDED
#define GAME_HPP_INCLUDED

#include "dansAudioLab.hpp"

#include <vector>

const int FPS=30;
const int TILE_SIZE=32;

enum Tile{ EMPTY, WALL, STAY_EMPTY, WATER };

struct Vertex{
	Vertex(float x, float y, float r, float g, float b):
		x(x), y(y), r(r), g(g), b(b)
	{}
	float x, y, r, g, b;
};

struct Object{//object size is equal to tile size
	Object():
		vx(0), vy(0), impulseX(0), impulseY(0), framesSinceGrounded(1),
		bumped(true), splashed(0)
	{}
	void setPosition(float x, float y);
	void update();
	float x, y, px, py, vx, vy, impulseX, impulseY;
	unsigned framesSinceGrounded;
	bool bumped;
	int splashed;
};

class Tiles{
	public:
		void resize(unsigned width, unsigned height){
			w=width;
			h=height;
			tiles.resize(width*height, WALL);
			mondrianL.resize(width*height, 0.0f);
			mondrianR.resize(width*height, 0.0f);
			mondrianU.resize(width*height, 0.0f);
			mondrianD.resize(width*height, 0.0f);
		}
		Tile at(int x, int y) const{
			if(x<0||x>=w||y<0||y>=h) return WALL;
			return tiles[x*h+y];
		}
		float& mondrianLAt(int x, int y){
			if(x<0||x>=w||y<0||y>=h) return mondrianL[0];
			return mondrianL[x*h+y];
		}
		float& mondrianRAt(int x, int y){
			if(x<0||x>=w||y<0||y>=h) return mondrianR[0];
			return mondrianR[x*h+y];
		}
		float& mondrianUAt(int x, int y){
			if(x<0||x>=w||y<0||y>=h) return mondrianU[0];
			return mondrianU[x*h+y];
		}
		float& mondrianDAt(int x, int y){
			if(x<0||x>=w||y<0||y>=h) return mondrianD[0];
			return mondrianD[x*h+y];
		}
		void set(int x, int y, Tile tile){
			if(x<0||x>=w||y<0||y>=h) return;
			tiles[x*h+y]=tile;
		}
		unsigned readW() const{ return w; }
		unsigned readH() const{ return h; }
	private:
		std::vector<Tile> tiles;
		std::vector<float> mondrianL;
		std::vector<float> mondrianR;
		std::vector<float> mondrianU;
		std::vector<float> mondrianD;
		unsigned w, h;
};

struct Cave{
	static void hole(
		unsigned x, unsigned y, float size,
		int platformStep, int platformSize, int platformSpace,
		int platformXOffset, int platformYOffset,
		bool platforms,
		Tiles& tiles
	);
	
	Cave(
		unsigned xi, unsigned yi, unsigned xf, unsigned yf,
		float size, bool platforms, int depth
	):
		xi(xi), yi(yi), xf(xf), yf(yf),
		size(size), platforms(platforms), depth(depth)
	{}
	
	void implement(Tiles& tiles);
	bool addBranch(unsigned& x, unsigned& y);
	
	unsigned xi, yi, xf, yf;
	float size;
	bool platforms;
	std::vector<float> branches;
	int depth;
	std::vector<unsigned> children;
	unsigned parent;
	unsigned connectionY;
};

class Game{
	public:
		int mondrianize(int x, int y, int dx, int dy, float size, bool lo);
		Game(dal::System* system);
		void jumpPressed();
		void jumpReleased();
		void leftPressed();
		void leftReleased();
		void rightPressed();
		void rightReleased();
		unsigned readW() const{ return tiles.readW(); }
		unsigned readH() const{ return tiles.readH(); }
		void getQuadVertices(unsigned width, unsigned height, std::vector<Vertex>&);
		int update();
	private:
		static const unsigned GRAVITY=TILE_SIZE*24;//pixels per second per second
		static const unsigned PLAYER_R=1;
		static const unsigned PLAYER_G=0;
		static const unsigned PLAYER_B=0;
		void updateSquare(
			Object& square, bool jumping, bool left, bool right,
			float jumpPitch, float jumpVolume, dal::Component* jumpComponent,
			unsigned hiJumpsCollected, bool scubaCollected, bool doSplash
		);
		void collideWithTiles(Object&, bool scubaCollected, float volume, bool doSplash);
		Object player, camera, buddy;
		std::vector<Object> hiJumps;
		Object scuba;
		Tiles tiles;
		bool playerJumping, playerGoingRight, playerGoingLeft;
		bool buddyGoingRight, buddyGoingLeft;
		int victory;
		dal::Component* playerJump;
		dal::Component* buddyJump;
		dal::Component* playerBump;
		dal::Component* powerup;
		dal::Component* splash;
		unsigned playerHiJumpsCollected;
		bool scubaCollected;
		//these are members just because it's easier to debug this way
		unsigned playerCave;
		std::vector<Cave> caves;
		unsigned seed;
};

#endif
