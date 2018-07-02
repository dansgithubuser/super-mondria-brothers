#include "game.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <set>

using namespace std;
using namespace dal;

//=====helpers=====//
bool intersects(
	float xi1, float yi1, float dx1, float dy1,
	float xi2, float yi2, float dx2, float dy2,
	float tAllowance, float sAllowance
){
	if(dx2*dy1-dy2*dx1==0) return false;
	if(dx2!=0){
		float t=(yi2+dy2*(xi1-xi2)/dx2-yi1)*dx2/(dx2*dy1-dy2*dx1);
		if(t<tAllowance||t>1-tAllowance) return false;
		float s=(xi1+dx1*t-xi2)/dx2;
		if(s<sAllowance||s>1-sAllowance) return false;
	}
	else{
		float t=-(xi2+dx2*(yi1-yi2)/dy2-xi1)*dy2/(dx2*dy1-dy2*dx1);
		if(t<tAllowance||t>1-tAllowance) return false;
		float s=(yi1+dy1*t-yi2)/dy2;
		if(s<sAllowance||s>1-sAllowance) return false;
	}
	return true;
}

float linear(float a, float b, float bness){
	return a*(1-bness)+b*bness;
}

int clamp(int i, int lo, int hi){
	if(i<lo) return lo;
	if(i>hi) return hi;
	return i;
}

void getInitialTerminalCaves(
	unsigned cave,
	const vector<Cave>& caves,
	set<unsigned>& initialTerminalCaves,
	set<unsigned> alreadyVisited=set<unsigned>(),
	int platformlessCavesPassed=0
){
	//only visit a cave once
	if(alreadyVisited.find(cave)!=alreadyVisited.end()) return;
	alreadyVisited.insert(cave);
	//handle platformless cave
	if(!caves[cave].platforms) ++platformlessCavesPassed;
	//next caves
	for(int i=-1; i<int(caves[cave].children.size()); ++i){
		unsigned nextCave;
		if(i==-1) nextCave=caves[cave].parent;
		else nextCave=caves[cave].children[i];
		//don't go to unreachable caves
		if(!caves[cave].platforms){
			//if the next cave is not at the bottom
			if(i!=-1&&caves[nextCave].connectionY-2>min(caves[cave].yi, caves[cave].yf))
				//don't process it
				continue;
		}
		//handle passing a platformless cave
		if(platformlessCavesPassed!=0){
			if(!caves[cave].children.size())
				initialTerminalCaves.insert(cave);
		}
		//recurse
		getInitialTerminalCaves(
			nextCave, caves, initialTerminalCaves, alreadyVisited,
			platformlessCavesPassed
		);
	}
}

void getCavesPastHiJumps(
	unsigned cave,
	const vector<Cave>& caves,
	set<unsigned>& cavesPastHiJumps,
	set<unsigned> alreadyVisited=set<unsigned>(),
	bool hiJumpCavePassed=false
){
	//only visit a cave once
	if(alreadyVisited.find(cave)!=alreadyVisited.end()) return;
	alreadyVisited.insert(cave);
	//if past a hi jump, put this cave in the result
	if(hiJumpCavePassed)
		cavesPastHiJumps.insert(cave);
	//next caves
	for(int i=-1; i<int(caves[cave].children.size()); ++i){
		unsigned nextCave;
		if(i==-1) nextCave=caves[cave].parent;
		else nextCave=caves[cave].children[i];
		//hi jump passages
		if(!caves[cave].platforms){
			//if the next cave is not at the bottom
			if(i!=-1&&caves[nextCave].connectionY-12>min(caves[cave].yi, caves[cave].yf)){
				//go to it, but mark it accordingly
				getCavesPastHiJumps(
					nextCave, caves, cavesPastHiJumps, alreadyVisited,
					true
				);
				continue;
			}
		}
		//recurse
		getCavesPastHiJumps(
			nextCave, caves, cavesPastHiJumps, alreadyVisited,
			hiJumpCavePassed
		);
	}
}

void pushTile(float x, float y, float w, float h, float r, float g, float b, vector<Vertex>& vertices){
	vertices.push_back(Vertex(x  , y  , r, g, b));
	vertices.push_back(Vertex(x+w, y  , r, g, b));
	vertices.push_back(Vertex(x+w, y+h, r, g, b));
	vertices.push_back(Vertex(x  , y+h, r, g, b));
}

//=====struct Object=====//
void Object::setPosition(float _x, float _y){
	x=_x;
	y=_y;
	px=x;
	py=y;
}

void Object::update(){
	px=x;
	py=y;
	if(impulseX){
		x+=impulseX;
		impulseX=0;
	}
	else x+=vx/FPS;
	if(impulseY){
		y+=impulseY;
		impulseY=0;
	}
	else y+=vy/FPS;
	++framesSinceGrounded;
}

//=====struct Cave=====//
void Cave::hole(
	unsigned x, unsigned y, float size,
	int platformStep, int platformSize, int platformSpace,
	int platformXOffset, int platformYOffset,
	bool platforms,
	Tiles& tiles
){
	x+=size/4*(1.0f*rand()/RAND_MAX-0.5f);
	y+=size/4*(1.0f*rand()/RAND_MAX-0.5f);
	if(platforms){
		for(int i=max(x-size, 0.0f); i<=min(x+size, tiles.readW()-1.0f); ++i)
			for(int j=max(y-size, 0.0f); j<=min(y+size, tiles.readH()-1.0f); ++j)
				if((i-x)*(i-x)+(j-y)*(j-y)<size*size){
					bool isPlatform=false;
					int platformI=i+platformXOffset*j/platformStep;
					if((j+platformI/platformSpace*platformYOffset)%platformStep==0)
						if(platformI%platformSpace<platformSize)
							isPlatform=true;
					if(isPlatform){
						if(tiles.at(i, j)!=STAY_EMPTY)
							tiles.set(i, j, WALL);
					}
					else tiles.set(i, j, EMPTY);
				}
	}
	else
		for(int i=max(x-size, 0.0f); i<=min(x+size, tiles.readW()-1.0f); ++i)
			for(int j=max(y-size, 0.0f); j<=min(y+size, tiles.readH()-1.0f); ++j)
				tiles.set(i, j, STAY_EMPTY);
}
	
void Cave::implement(Tiles& tiles){
	unsigned d=max(abs(int(xf)-int(xi)), abs(int(yf)-int(yi)));
	const int platformStep=3+rand()%2;
	const int platformSize=2+rand()%2;
	const int platformSpace=platformSize+1+rand()%6;
	const int platformXOffset=1+rand()%(platformSpace-1);
	const int platformYOffset=rand()%platformStep;
	if(d==0){
		hole(
			xi, yi, size*(1+1.0f*rand()/RAND_MAX),
			platformStep, platformSize, platformSpace,
			platformXOffset, platformYOffset,
			platforms,
			tiles
		);
		return;
	}
	for(unsigned i=0; i<=d; ++i)
		hole(
			linear(xi, xf, 1.0f*i/d), linear(yi, yf, 1.0f*i/d), size,
			platformStep, platformSize, platformSpace,
			platformXOffset, platformYOffset,
			platforms,
			tiles
		);
}

bool Cave::addBranch(unsigned& x, unsigned& y){
	if(branches.size()>=3) return false;
	while(true){
		float t=1.0f*rand()/RAND_MAX;
		bool good=true;
		for(unsigned i=0; i<branches.size(); ++i)
			if(abs(t-branches[i])<0.2f){
				good=false;
				break;
			}
		if(!good) continue;
		branches.push_back(t);
		x=linear(xi, xf, t);
		y=linear(yi, yf, t);
		break;
	}
	return true;
}

//=====class Game=====//
int Game::mondrianize(int x, int y, int dx, int dy, float size, bool lo){
	int n=0;
	while(x>=0&&y>=0&&x<tiles.readW()&&y<tiles.readH()){
		bool willBreak=false;
		if(dx>0&&tiles.mondrianLAt(x, y)!=0.0f) break;
		if(dy>0&&tiles.mondrianDAt(x, y)!=0.0f) break;
		if(dx<0&&tiles.mondrianRAt(x, y)!=0.0f) break;
		if(dy<0&&tiles.mondrianUAt(x, y)!=0.0f) break;
		if(
			tiles.mondrianLAt(x, y)!=0.0f||
			tiles.mondrianRAt(x, y)!=0.0f||
			tiles.mondrianUAt(x, y)!=0.0f||
			tiles.mondrianDAt(x, y)!=0.0f
		)
			willBreak=true;
		if(size>=0){
			if(dx!=0){
				if(lo)
					tiles.mondrianDAt(x, y)=size;
				else
					tiles.mondrianUAt(x, y)=size;
			}
			else{//dy!=0
				if(lo)
					tiles.mondrianLAt(x, y)=size;
				else
					tiles.mondrianRAt(x, y)=size;
			}
		}
		if(willBreak) break;
		x+=dx;
		y+=dy;
		++n;
	}
	return n;
}

Game::Game(dal::System* system):
	playerJumping(false),
	playerGoingRight(false),
	playerGoingLeft(false),
	buddyGoingRight(false),
	buddyGoingLeft(false),
	victory(0),
	playerHiJumpsCollected(0),
	scubaCollected(false)
{
	//sound
	playerJump=&system->component("playerJump");
	buddyJump=&system->component("buddyJump");
	playerBump=&system->component("playerBump");
	powerup=&system->component("powerup");
	splash=&system->component("splash");
	//initialize
	unsigned seed=unsigned(time(NULL));
	srand(seed);
	tiles.resize(256, 256);
	//MONDRIANIZE ME CAPTAIN
	for(unsigned i=0; i<tiles.readW(); ++i){
		float size=0.1f+0.2f*rand()/RAND_MAX;
		int x=rand()%tiles.readW();
		int y=rand()%tiles.readH();
		bool lo=rand()%2;
		if(
			tiles.mondrianLAt(x, y)!=0.0f||
			tiles.mondrianRAt(x, y)!=0.0f||
			tiles.mondrianUAt(x, y)!=0.0f||
			tiles.mondrianDAt(x, y)!=0.0f
		) continue;
		int w=mondrianize(x, y, 1, 0, -1.0f, lo)+mondrianize(x, y, -1, 0, -1.0f, lo);
		int h=mondrianize(x, y, 0, 1, -1.0f, lo)+mondrianize(x, y, 0, -1, -1.0f, lo);
		if(w<h){
			mondrianize(x, y, 1, 0, size, lo);
			mondrianize(x-1, y, -1, 0, size, lo);
		}
		else{
			mondrianize(x, y, 0, 1, size, lo);
			mondrianize(x, y-1, 0, -1, size, lo);
		}
	}
	tiles.mondrianLAt(0, 0)=0.0f;
	tiles.mondrianRAt(0, 0)=0.0f;
	tiles.mondrianUAt(0, 0)=0.0f;
	tiles.mondrianDAt(0, 0)=0.0f;
	//make some caves
	const unsigned firstSize=5;
	const unsigned firstHeight=rand()%(tiles.readH()/2)+tiles.readH()/4+firstSize+1;
	caves.push_back(Cave(
		rand()%(tiles.readW()/4)+firstSize+1,
		firstHeight,
		rand()%(tiles.readW()/4)+tiles.readW()/2-firstSize-1,
		firstHeight+rand()%(tiles.readH()/4)-tiles.readH()/8,
		firstSize,
		true,
		0
	));
	caves.back().parent=0;
	vector<unsigned> queue;
	queue.push_back(0);
	bool madePlatformlessCave=false;
	while(queue.size()){
		//pick a parent
		unsigned i=rand()%queue.size();
		//choose whether or not child has platforms
		bool platforms=rand()%8;
		if(!madePlatformlessCave) platforms=false;
		//get location and size
		unsigned x, y;
		float size=caves[queue[i]].size/1.25f;
		if(caves[queue[i]].depth>3||size<2.0f||!caves[queue[i]].addBranch(x, y)){
			queue.erase(queue.begin()+i);
			continue;
		}
		//get perpendicular direction
		int dx=int(caves[queue[i]].yi)-int(caves[queue[i]].yf);
		int dy=int(caves[queue[i]].xf)-int(caves[queue[i]].xi);
		if(platforms){
			//maybe flip it
			if(rand()%2){
				dx=-dx;
				dy=-dy;
			}
		}
		else{
			//force it to be upward
			if(dy<0) dy=-dy;
			if(dy>32) dy=32;
			dx=0;
		}
		//change length
		dx*=0.5f*rand()/RAND_MAX+1.0f;
		dy*=0.5f*rand()/RAND_MAX+1.0f;
		//noise
		if(platforms){
			float r=sqrt(dx*dx+dy*dy);
			dx+=r*(0.5f*rand()/RAND_MAX-0.25f);
			dy+=r*(0.5f*rand()/RAND_MAX-0.25f);
		}
		//limit
		const int extra=4;
		dx=min(dx, int(tiles.readW()-size-extra-x));
		dx=max(dx, int(size+extra-x));
		dy=min(dy, int(tiles.readH()-size-extra-y));
		dy=max(dy, int(size+extra-y));
		//check if it overlaps with other caves
		bool overlaps=false;
		for(unsigned i=0; i<caves.size(); ++i){
			int dx2=int(caves[i].xf)-int(caves[i].xi);
			int dy2=int(caves[i].yf)-int(caves[i].yi);
			if(intersects(
				x, y,
				dx, dy,
				caves[i].xi, caves[i].yi,
				dx2, dy2,
				(caves[i].size+size)/(abs(dx)+abs(dy)),
				(caves[i].size+size)/(abs(dx2)+abs(dy2))
			)){
				overlaps=true;
				break;
			}
		}
		if(overlaps) continue;
		//stick it in
		if(!platforms) madePlatformlessCave=true;
		caves.push_back(Cave(
			x,
			y,
			clamp(int(x)+dx, 0, tiles.readW()-1),
			clamp(int(y)+dy, 0, tiles.readH()-1),
			size,
			platforms,
			caves[queue[i]].depth+1
		));
		queue.push_back(unsigned(caves.size()-1));
		//attach
		caves[queue[i]].children.push_back((unsigned)caves.size()-1);
		caves.back().parent=queue[i];
		caves.back().connectionY=y;
	}
	for(unsigned i=0; i<caves.size(); ++i) caves[i].implement(tiles);
	//turn STAY_EMPTY into EMPTY
	for(unsigned x=0; x<tiles.readW(); ++x)
		for(unsigned y=0; y<tiles.readH(); ++y)
			if(tiles.at(x, y)==STAY_EMPTY)
				tiles.set(x, y, EMPTY);
	//get rid of diagonals
	for(unsigned x=0; x<tiles.readW(); ++x)
		for(unsigned y=0; y<tiles.readH(); ++y)
			if(
				tiles.at(x, y)==tiles.at(x+1, y+1)
				&&
				tiles.at(x+1, y)==tiles.at(x, y+1)
				&&
				tiles.at(x, y)!=tiles.at(x+1, y)
			){
				tiles.set(x, y, EMPTY);
				tiles.set(x+1, y, EMPTY);
				tiles.set(x, y+1, EMPTY);
				tiles.set(x+1, y+1, EMPTY);
			}
	//add water on the right half
	bool waterPlaced=false;
	for(int y=tiles.readH()-1; y>=0; --y)
		for(int x=tiles.readW()-1; x>tiles.readW()/2; --x){
			bool goodPlace=false;
			for(unsigned i=0; i<caves.size(); ++i){
				int midX=(caves[i].xi+caves[i].xf)/2;
				int midY=(caves[i].yi+caves[i].yf)/2;
				if(x==midX&&abs(y-midY)<6) goodPlace=true;
			}
			if(!goodPlace) continue;
			if(
				tiles.at(x, y)==EMPTY
				&&
				tiles.at(x, y+1)==WALL
			){
				if(waterPlaced){ if(rand()%2) continue; }
				else waterPlaced=true;
				vector<pair<int, int> > waterQueue;
				waterQueue.push_back(pair<int, int>(x, y));
				while(waterQueue.size()){
					int wx=waterQueue.back().first;
					int wy=waterQueue.back().second;
					waterQueue.pop_back();
					tiles.set(wx, wy, WATER);
					if(tiles.at(wx, wy-1)==EMPTY) waterQueue.push_back(pair<int, int>(wx, wy-1));
					else{
						if(tiles.at(wx+1, wy)==EMPTY) waterQueue.push_back(pair<int, int>(wx+1, wy));
						if(tiles.at(wx-1, wy)==EMPTY) waterQueue.push_back(pair<int, int>(wx-1, wy));
					}
				}
			}
		}
	//set the player's position to somewhere on the left
	int playerX, playerY;
	int desiredX=tiles.readW(), desiredY;
	for(unsigned i=0; i<caves.size(); ++i)
		if(caves[i].platforms){
			if(caves[i].xi<desiredX){
				playerCave=i;
				desiredX=caves[i].xi;
				desiredY=caves[i].yi;
			}
			if(caves[i].xf<desiredX){
				playerCave=i;
				desiredX=caves[i].xf;
				desiredY=caves[i].yf;
			}
		}
	for(int x=0; x<tiles.readW(); ++x)
		for(int y=0; y<tiles.readH(); ++y)
			if(abs(x-desiredX)<8&&abs(y-desiredY)<8)
				if(tiles.at(x, y)==EMPTY&&tiles.at(x, y-1)==WALL){
					playerX=x;
					playerY=y;
					player.setPosition(TILE_SIZE*x, TILE_SIZE*y);
					x=tiles.readW();//break out of outer loop too
					break;
				}
	camera=player;
	//add scuba suit somewhere accessible to the player
	vector<pair<int, int> > scubaQueue;
	scubaQueue.push_back(pair<int, int>(playerX, playerY));
	set<pair<int, int> > potentialScubas;
	set<pair<int, int> > visited;
	while(scubaQueue.size()){
		int x=scubaQueue.back().first;
		int y=scubaQueue.back().second;
		scubaQueue.pop_back();
		if(visited.find(pair<int, int>(x, y))!=visited.end()) continue;
		visited.insert(pair<int, int>(x, y));
		if(x>tiles.readW()/3) continue;
		if(tiles.at(x-1, y)==EMPTY||tiles.at(x+1, y)==EMPTY)
			potentialScubas.insert(pair<int, int>(x, y));
		if(tiles.at(x+1, y)==EMPTY) scubaQueue.push_back(pair<int, int>(x+1, y));
		if(tiles.at(x-1, y)==EMPTY) scubaQueue.push_back(pair<int, int>(x-1, y));
		if(tiles.at(x, y-1)==EMPTY) scubaQueue.push_back(pair<int, int>(x, y-1));
	}
	int scubaX, scubaY;
	float furthest=0.0f;
	for(int x=0; x<=tiles.readW()/3; ++x)
		for(int y=0; y<tiles.readH(); ++y)
			if(potentialScubas.find(pair<int, int>(x, y))!=potentialScubas.end()){
				float distance=abs(x-playerX)+abs(y-playerY);
				if(distance>furthest){
					scubaX=x;
					scubaY=y;
					furthest=distance;
				}
			}
	scuba.setPosition(TILE_SIZE*scubaX, TILE_SIZE*scubaY);
	//set the buddy position to somewhere past a hi jump, prefer being on the right
	set<unsigned> cavesPastHiJumps;
	getCavesPastHiJumps(playerCave, caves, cavesPastHiJumps);
	for(int x=tiles.readW()-1; x>=0; --x)
		for(int y=0; y<tiles.readH(); ++y){
			bool pastHiJump=false;
			if(!cavesPastHiJumps.size()) pastHiJump=true;
			for(
				set<unsigned>::iterator i=cavesPastHiJumps.begin();
				i!=cavesPastHiJumps.end();
				++i
			){
				if(
					(abs(x-int(caves[*i].xi))<8&&abs(y-int(caves[*i].yi))<8)
					||
					(abs(x-int(caves[*i].xf))<8&&abs(y-int(caves[*i].yf))<8)
				)
					pastHiJump=true;
			}
			if(pastHiJump&&tiles.at(x, y)==EMPTY&&tiles.at(x, y-1)==WALL){
				buddy.setPosition(TILE_SIZE*x, TILE_SIZE*y);
				x=-1;//break out of outer loop too
				break;
			}
		}
	//set the hi jump location to somewhere accessible with no powerups from start
	set<unsigned> initiallyTerminalCaves;
	getInitialTerminalCaves(playerCave, caves, initiallyTerminalCaves);
	for(
		set<unsigned>::iterator i=initiallyTerminalCaves.begin();
		i!=initiallyTerminalCaves.end();
		++i
	){
		Object hiJump;
		if(!caves[*i].platforms)
			hiJump.setPosition(TILE_SIZE*caves[*i].xi, TILE_SIZE*caves[*i].yi);
		else{
			desiredX=caves[*i].xf;
			desiredY=caves[*i].yf;
			for(int x=0; x<tiles.readW(); ++x)
				for(int y=0; y<tiles.readH(); ++y)
					if(abs(x-desiredX)<4&&abs(y-desiredY)<4)
						if(tiles.at(x, y)==EMPTY&&tiles.at(x, y-1)==WALL){
							hiJump.setPosition(TILE_SIZE*x, TILE_SIZE*y);
							x=tiles.readW();//break out of outer loop too
							break;
						}
		}
		hiJumps.push_back(hiJump);
	}
}

void Game::jumpPressed(){ playerJumping=true; }
void Game::jumpReleased(){ playerJumping=false; }

void Game::leftPressed(){
	playerGoingLeft=true;
	player.impulseX=-TILE_SIZE;
}

void Game::leftReleased(){
	playerGoingLeft=false;
	player.vx/=2;
}

void Game::rightPressed(){
	playerGoingRight=true;
	player.impulseX=TILE_SIZE;
}

void Game::rightReleased(){
	playerGoingRight=false;
	player.vx/=2;
}

void Game::getQuadVertices(unsigned width, unsigned height, vector<Vertex>& vertices){
	int xi=int((camera.x-width /2)/TILE_SIZE-1);
	int yi=int((camera.y-height/2)/TILE_SIZE-1);
	int xf=int((camera.x+width /2)/TILE_SIZE);
	int yf=int((camera.y+height/2)/TILE_SIZE);
	for(int x=xi; x<=xf; ++x)
		for(int y=yi; y<=yf; ++y){
			float r=0.0f, g=0.0f, b=0.0f;
			switch(tiles.at(x, y)){
				case WALL : r=1.0f; g=1.0f; b=1.0f; break;
				case WATER: r=0.0f; g=0.0f; b=1.0f; break;
				default: break;
			}
			if(tiles.at(x, y)==WALL){
				pushTile(
					TILE_SIZE*x-camera.x+TILE_SIZE*tiles.mondrianLAt(x, y),
					TILE_SIZE*y-camera.y+TILE_SIZE*tiles.mondrianDAt(x, y),
					(1-tiles.mondrianLAt(x, y)-tiles.mondrianRAt(x, y))*TILE_SIZE,
					(1-tiles.mondrianDAt(x, y)-tiles.mondrianUAt(x, y))*TILE_SIZE,
					r, g, b, vertices
				);
			}
			else{
				pushTile(
					TILE_SIZE*x-camera.x,
					TILE_SIZE*y-camera.y,
					TILE_SIZE,
					TILE_SIZE,
					r, g, b, vertices
				);
			}
		}
	pushTile(
		int(player.x/TILE_SIZE)*TILE_SIZE-camera.x,
		int(player.y/TILE_SIZE)*TILE_SIZE-camera.y,
		TILE_SIZE, TILE_SIZE,
		1.0f*PLAYER_R, 1.0f*PLAYER_G, 1.0f*PLAYER_B,
		vertices
	);
	pushTile(
		int(buddy.x/TILE_SIZE)*TILE_SIZE-camera.x,
		int(buddy.y/TILE_SIZE)*TILE_SIZE-camera.y,
		TILE_SIZE, TILE_SIZE,
		1.0f*PLAYER_R, 1.0f*PLAYER_G, 1.0f*PLAYER_B,
		vertices
	);
	for(unsigned i=0; i<hiJumps.size(); ++i)
		pushTile(
			int(hiJumps[i].x/TILE_SIZE)*TILE_SIZE-camera.x,
			int(hiJumps[i].y/TILE_SIZE)*TILE_SIZE-camera.y,
			TILE_SIZE, TILE_SIZE,
			1.0f, 1.0f, 0.0f,
			vertices
		);
	if(!scubaCollected){
		pushTile(
			int(scuba.x/TILE_SIZE)*TILE_SIZE-camera.x,
			int(scuba.y/TILE_SIZE)*TILE_SIZE-camera.y,
			TILE_SIZE, TILE_SIZE,
			0.0f, 0.0f, 1.0f,
			vertices
		);
	}
}

int Game::update(){
	float jumpVolume=0.2f;
	//player
	updateSquare(
		player, playerJumping,
		playerGoingLeft, playerGoingRight,
		440.0f, jumpVolume, playerJump, playerHiJumpsCollected, scubaCollected, true
	);
	//hi jumps
	for(unsigned i=0; i<hiJumps.size(); NULL){
		if(abs(hiJumps[i].x-player.x)<TILE_SIZE&&abs(hiJumps[i].y-player.y)<TILE_SIZE){
			++playerHiJumpsCollected;
			hiJumps.erase(hiJumps.begin()+i);
			powerup->perform("", &jumpVolume);
		}
		else ++i;
	}
	//scuba
	if(!scubaCollected&&abs(scuba.x-player.x)<TILE_SIZE&&abs(scuba.y-player.y)<TILE_SIZE){
		scubaCollected=true;
		powerup->perform("", &jumpVolume);
	}
	//buddy
	float attenuation=
		(player.x-buddy.x)*(player.x-buddy.x)
		+
		(player.y-buddy.y)*(player.y-buddy.y)
	;
	if(abs(player.x-buddy.x)+abs(player.y-buddy.y)<TILE_SIZE*12){
		if(player.x>buddy.x){
			buddyGoingRight=true;
			buddyGoingLeft=false;
		}
		else{
			buddyGoingLeft=true;
			buddyGoingRight=false;
		}
	}
	attenuation/=1000.0f*TILE_SIZE*TILE_SIZE;
	attenuation=max(1.0f, attenuation);
	updateSquare(
		buddy, rand()%(FPS*8)==0||(victory&&rand()%(FPS)==0),
		buddyGoingLeft, buddyGoingRight,
		330.0f, jumpVolume/attenuation, buddyJump, 0, false, false
	);
	//victory
	if(abs(player.x-buddy.x)+abs(player.y-buddy.y)<TILE_SIZE*6) ++victory;
	//camera
	const float cameraLag=1.5f;
	camera.vx+=(player.x+8*player.vx/FPS-camera.x)/cameraLag;
	camera.vy+=(player.y+8*player.vy/FPS-camera.y)/cameraLag;
	camera.update();
	const float cameraFriction=1.2f;
	camera.vx/=cameraFriction;
	camera.vy/=cameraFriction;
	return victory;
}

void Game::updateSquare(
	Object& square, bool jumping, bool left, bool right,
	float jumpPitch, float jumpVolume, Component* jumpComponent,
	unsigned hiJumpsCollected, bool scubaCollected, bool doSplash
){
	if(jumping){
		bool grounded=
			tiles.at(square.x/TILE_SIZE, square.y/TILE_SIZE-1)==WALL
			&&
			square.vy<=0
		;
		if(hiJumpsCollected||grounded){
			if(grounded) square.vy=20*TILE_SIZE;
			else square.vy=8*hiJumpsCollected*TILE_SIZE;
			jumpComponent->perform("", &jumpVolume);
		}
	}
	square.vy-=1.0f*GRAVITY/FPS;
	const float playerGroundMovement=TILE_SIZE*2;
	const float playerAirMovement=TILE_SIZE/2;
	if(right){
		if(square.framesSinceGrounded==0) square.vx+=playerGroundMovement;
		else square.vx+=playerAirMovement;
	}
	else if(left){
		if(square.framesSinceGrounded==0) square.vx-=playerGroundMovement;
		else square.vx-=playerAirMovement;
	}
	if(square.framesSinceGrounded!=0){
		const float playerAirFriction=1.01f;
		square.vx/=playerAirFriction;
		square.vy/=playerAirFriction;
	}
	const float speedLimit=TILE_SIZE*FPS;
	if(square.vx>speedLimit) square.vx=speedLimit;
	else if(square.vx<-speedLimit) square.vx=-speedLimit;
	if(square.vy>speedLimit) square.vy=speedLimit;
	else if(square.vy<-speedLimit) square.vy=-speedLimit;
	square.update();
	collideWithTiles(square, scubaCollected, jumpVolume, doSplash);
}

void Game::collideWithTiles(Object& object, bool scubaCollected, float volume, bool doSplash){
	const float collisionFriction=1.5f;
	int px=int(object.px/TILE_SIZE);
	int py=int(object.py/TILE_SIZE);
	bool done=false;
	int x, y;
	bool bumped=false;
	while(!done){
		x=int(object.x/TILE_SIZE);
		y=int(object.y/TILE_SIZE);
		switch(tiles.at(x, y)){
			case WALL:
				if(px!=x&&py!=y){
					object.vx/=collisionFriction;
					object.vy=0.0f;
					object.y=object.py;
					object.framesSinceGrounded=0;
					bumped=true;
					continue;
				}
				else if(px!=x){
					object.vx=0.0f;
					object.vy/=collisionFriction;
					object.x=object.px;
					object.framesSinceGrounded=0;
					bumped=true;
				}
				else if(py!=y){
					object.vx/=collisionFriction;
					object.vy=0.0f;
					object.y=object.py;
					object.framesSinceGrounded=0;
					bumped=true;
				}
				break;
			default: break;
		}
		done=true;
	}
	if(bumped&&!object.bumped){
		playerBump->perform("", &volume);
	}
	object.bumped=bumped;
	if(object.splashed>0) --object.splashed;
	if(doSplash&&!object.splashed&&tiles.at(x, y)==WATER){
		splash->perform("", &volume);
		object.splashed=30;
	}
	if(!scubaCollected&&tiles.at(x, y)==WATER){
		const float waterFriction=2.0f;
		object.vx/=waterFriction;
		object.vy/=waterFriction;
	}
}
