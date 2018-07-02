#include "dansAudioLab.hpp"

#include <fstream>
#include <algorithm>
#include <cmath>

using namespace dal;
using namespace std;

float dal::triangle(float phase){
	if(phase<0.5f){
		if(phase<0.25f){
			return 4*phase;
		}
		else{
			return 2-4*phase;
		}
	}
	else{
		if(phase<0.75f){
			return 2-4*phase;
		}
		else{
			return -4+4*phase;
		}
	}
}

/*=====MIDI=====*/
const unsigned TRACKHEADERSIZE=8;
const string TRACKTITLE="MTrk";//invariable part of track header
const unsigned HEADERSIZE=14;//file header size
const string HEADERTITLE="MThd";//invariable part of file header

/*-----helpers-----*/
//Return the bytes that specify a delta time equal to ticks.
static vector<unsigned char> delta(int ticks){
	vector<unsigned char> delta;
	vector<unsigned char> error;
	while(true){
		int byte=ticks&0x7f;//take 7 bits of ticks
		ticks=ticks>>7;//shift the 7 bits out of ticks so the next 7 are ready
		delta.insert(delta.begin(), byte);
		if(delta.size()>4) return error;//deltas can't be more than 4 bytes long
		if(ticks==0){
			//this bit says it's not the end of the delta... don't set it for the end.
			for(unsigned i=0; i<delta.size()-1; i++) delta[i]|=0x80;
			return delta;
		}
	}
}

//Return delta that starts at bytes[i], or -1 if something went wrong.
//i will be overwritten with the index just after the end of the delta.
//A delta is stored as a series of up to 4 bytes. 7 bits of each byte are the
//delta and the last bit says whether or not this is the last byte of the
//delta.
static int getDelta(const vector<unsigned char>& bytes, unsigned& i){
	int delta=0;
	int j=0;//how long the delta is in bytes
	while(true){
		//each byte represents a 7 bit chunk of the delta
		delta<<=7;
		delta+=bytes[i]&0x7f;
		//The most significant bit signals when to stop.
		if(!(bytes[i]&0x80)){
			++i;
			break;
		}
		//But a delta time should not be more than 4 bytes long.
		else if(j==3) return -1;
		++j;
		++i;
	}
	return delta;
}

//Go through trackChunk translating delta bytes to a number, and group them
//with their MIDI command bytes.
//Return empty vector if error occurred.
//trackChunk is assumed to have come from a track produced by chunkitize.
static vector<pair<int, vector<unsigned char> > > getCommands(
	const vector<unsigned char>& trackChunk
){
	vector<pair<int, vector<unsigned char> > > result;
	vector<pair<int, vector<unsigned char> > > error;///just use constructor
	pair<int, vector<unsigned char> > empty;//to push_back into result
	unsigned i=TRACKHEADERSIZE;//start after the header of the track
	unsigned char last=0;//the last command type and channel given
	while(i<trackChunk.size()){
		result.push_back(empty);//make space for a new delta and command pair
		//delta
		int delta=getDelta(trackChunk, i);
		if(delta==-1) return error;
		result.back().first=delta;
		//command
		unsigned char status=trackChunk[i];//running status
		bool running=false;
		if((status&0x80)==0){
			status=last;
			result.back().second.push_back(status);
			running=true;
		}
		int type=status>>4;
		if(type==0x8||type==0x9||type==0xa||type==0xb||type==0xe){
			//these 5 MIDI command types are 3 bytes long, including command type
			for(int j=0; j<(running?2:3); j++){
				result.back().second.push_back(trackChunk[i]);
				i++;
			}
			last=status;
		}
		else if(type==0xc||type==0xd){
			//these 2 are 2 bytes long, including command type
			for(int j=0; j<(running?1:2); j++){
				result.back().second.push_back(trackChunk[i]);
				i++;
			}
			last=status;
		}
		else if(type==0xf){
			//this type varies in size...
			if(status==(unsigned char)0xff){//this is called a meta event
				int size=3+trackChunk[i+2];//its size is specified here
				for(int j=0; j<size; j++){
					result.back().second.push_back(trackChunk[i]);
					i++;
				}
			}
			//these are just one byte
			else{
				result.back().second.push_back(trackChunk[i]);
				i++;
			}
		}
		else return error;
	}
	//The last event should be a meta event which means "end of track".
	//So result.back() should be {delta, 0xff, 0x2f}
	if(result.size()==0) return result;
	if(
		result.back().second.size()<2
		||result.back().second[0]!=(unsigned char)0xff
		||result.back().second[1]!=(unsigned char)0x2f
	)
		return error;
	return result;
}

//return an unsigned integer from a big endian string of bytes
static unsigned bToU(vector<unsigned char>::iterator i, int size){
	unsigned result=0;
	for(int j=0; j<size; j++){
		result<<=8;
		result+=*i;
		i++;
	}
	return result;
}

//group the bytes of a MIDI file into header and tracks
static vector<vector<unsigned char> > chunkitize(vector<unsigned char>& bytes){
	vector<vector<unsigned char> > error;
	//check for the header
	if(bytes.size()<HEADERSIZE) return error;
	for(unsigned i=0; i<HEADERTITLE.size(); i++)
		if(bytes[i]!=HEADERTITLE[i]) return error;
	//put in the header
	vector<vector<unsigned char> > chunks;
	vector<unsigned char> empty;
	chunks.push_back(empty);
	unsigned i;
	for(i=0; i<HEADERSIZE; i++) chunks[0].push_back(bytes[i]);
	//put in each track
	while(bytes.size()>=i+TRACKHEADERSIZE){
		//check the track
		if(bytes.size()<i+TRACKHEADERSIZE) return error;
		for(unsigned j=0; j<TRACKTITLE.size(); j++)
			if(bytes[j+i]!=TRACKTITLE[j]) return error;
		//tracks' headers say their size, so check it's right
		int trackSize=bToU(bytes.begin()+i+4, 4);
		if(bytes.size()<i+TRACKHEADERSIZE+trackSize) return error;
		//if it's good, actually go ahead and put it in
		chunks.push_back(empty);
		for(unsigned j=0; j<TRACKHEADERSIZE+trackSize; j++){
			chunks.back().push_back(bytes[i]);
			i++;
		}
	}
	//make sure we're done
	if(bytes[i]!=(unsigned char)EOF) return error;
	//check that the file header specified the correct number of tracks
	if(bToU(bytes.begin()+10, 2)!=chunks.size()-1) return error;
	//return successfully created result
	return chunks;
}


//Write a midi track to a file.
//The track header and end message are appended automatically,
//so they should not be included in bytes.
static void writeTrack(ofstream& file, const vector<unsigned char>& bytes){
	for(unsigned i=0; i<TRACKTITLE.size(); i++) file.put(TRACKTITLE[i]);
	//put in the size of the track in 4 bytes
	int extra=8;//for the upcoming additions
	if(bytes.size()&&bytes[0]==0) extra=4;
	file.put((bytes.size()+extra)>>24);
	file.put(((bytes.size()+extra)>>16)&0xff);
	file.put(((bytes.size()+extra)>>8)&0xff);
	file.put((bytes.size()+extra)&0xff);
	//some idiot midi players ignore the first delta time,
	//so insert an empty text event if needed
	if(!bytes.size()||bytes[0]) file.write("\x00\xff\x01\x00", 4);
	//write the given bytes
	for(unsigned i=0; i<bytes.size(); i++) file.put(bytes[i]);
	//the delta time of 1 is to match what Sibelius 2 does.
	//Don't know if it does anything.
	file.write("\x01\xff\x2f\x00", 4);
}

static bool compareTime(Midi::Event a, Midi::Event b){
	return a.timeInTicks<b.timeInTicks;
}

//integer part of inverse log base 2
//-1 means -infinity
static int iLog2(int x){
	int result=-1;
	while(x){
		x>>=1;
		result++;
	}
	return result;
}

/*-----Midi::Event-----*/
float Midi::Event::freq(){
	return float(8.1757989f*pow(2.0, ((sharps*7+(minor?9:0))%12)/12.0));
}

unsigned Midi::Event::root(){
	if(sharps>0) return (sharps*7+(minor?9:0))%12;
	else return (-sharps*5+(minor?9:0))%12;
}

unsigned Midi::Event::tpb(int tpq){
	return tpq*timeSigTop/timeSigBottom;
}

int Midi::Event::endTicks(){
	return timeInTicks+(type==NOTE?duration:0);
}

/*-----Midi-----*/
string Midi::read(string filename){
	vector<unsigned char> bytes;
	ifstream file;
	file.open(filename.c_str(), ios_base::binary);
	if(!file.is_open()) return "Couldn't open file or couldn't find file.";
	while(file.good()) bytes.push_back(file.get());
	file.close();
	return parse(bytes);
}


bool Midi::write(string filename){
	if(ticksPerQuarter==0) return false;
	ofstream file;
	file.open(filename.c_str(), ios_base::binary);
	if(!file.is_open()) return false;
	//write the file header
	for(unsigned i=0; i<HEADERTITLE.size(); i++) file.put(HEADERTITLE[i]);
	//file header is 6 bytes long (always), midi file type is 1
	file.write("\x00\x00\x00\x06\x00\x01\x00", 7);
	file.put(tracks.size());//probably not going to be over 255
	file.put(ticksPerQuarter>>8);//high bits
	file.put(ticksPerQuarter&0xff);//low bits
	for(unsigned i=0; i<tracks.size(); i++){//for all tracks
		vector<Event> events;
		//split note events into note on and note off
		for(unsigned j=0; j<tracks[i].size(); j++){
			events.push_back(tracks[i][j]);
			if(events.back().type==Midi::Event::NOTE){
				Event temp=events.back();
				events.back().velocityUp=-1;
				temp.velocityDown=-1;
				temp.timeInTicks+=temp.duration;
				events.push_back(temp);
			}
		}
		//sort
		sort(events.begin(), events.end(), compareTime);
		//write
		int lastTimeInTicks=0;//time of last event, so we can calculate delta
		vector<unsigned char> bytes;//the bytes we are going to give writeTrack
		for(unsigned j=0; j<events.size(); j++){//for all events
			//write the delta
			vector<unsigned char> d=delta(events[j].timeInTicks-lastTimeInTicks);
			if(d.size()==0){
				file.close();
				return false;
			}
			for(unsigned k=0; k<d.size(); k++) bytes.push_back(d[k]);
			//write the event, based on its type
			if(events[j].type==Midi::Event::NOTE){
				if(events[j].velocityUp==-1){//note on
					bytes.push_back(0x90|events[j].channel);
					bytes.push_back(events[j].note);
					bytes.push_back(events[j].velocityDown);
				}
				else{//note off
					bytes.push_back(0x80|events[j].channel);
					bytes.push_back(events[j].note);
					bytes.push_back(events[j].velocityUp);
				}
			}
			else if(events[j].type==Midi::Event::TEMPO){
				bytes.push_back(0xff);
				bytes.push_back(0x51);
				bytes.push_back(0x03);
				bytes.push_back(events[j].usPerQuarter>>16);
				bytes.push_back((events[j].usPerQuarter>>8)&0xff);
				bytes.push_back((events[j].usPerQuarter)&0xff);
			}
			else if(events[j].type==Midi::Event::TIME){
				bytes.push_back(0xff);
				bytes.push_back(0x58);
				bytes.push_back(0x04);
				bytes.push_back(events[j].timeSigTop);
				bytes.push_back(iLog2(events[j].timeSigBottom));
				bytes.push_back(24);
				bytes.push_back(8);
			}
			else if(events[j].type==Midi::Event::KEY){
				bytes.push_back(0xff);
				bytes.push_back(0x59);
				bytes.push_back(0x02);
				bytes.push_back(events[j].sharps);
				bytes.push_back(events[j].minor?1:0);
			}
			else if(events[j].type==Midi::Event::TEXT){
				bytes.push_back(0xff);
				bytes.push_back(0x01);
				bytes.push_back(events[j].text.size());
				for(unsigned k=0; k<events[j].text.size(); ++k)
					bytes.push_back(events[j].text[k]);
			}
			lastTimeInTicks=events[j].timeInTicks;
		}//end of for all events
		writeTrack(file, bytes);
	}//end of for all tracks
	file.close();
	return true;
}

int Midi::getUsPerQuarter(){
	if(tracks.empty()) return 0;
	for(unsigned i=0; i<tracks[0].size(); ++i)
		if(tracks[0][i].type==Midi::Event::TEMPO)
			return tracks[0][i].usPerQuarter;
	return 0;
}

//parse bytes of a MIDI file, populate self
string Midi::parse(vector<unsigned char>& bytes){
	vector<vector<unsigned char> > chunks=chunkitize(bytes);
	if(chunks.size()==0) return "Couldn't chunkitize.";
	ticksPerQuarter=bToU(chunks[0].begin()+12, 2);
	if(ticksPerQuarter==0) return "Ticks per quarter is 0.";
	if(bToU(chunks[0].begin()+8, 2)!=1) return "Midi is not type 1 file.";
	if(chunks.size()<=1) return "";//we're done!
	for(unsigned i=1; i<chunks.size(); i++){//for all tracks
		int ticks=0;//keep track of how much time has passed; want absolute time
		vector<pair<int, vector<unsigned char> > > commands=getCommands(chunks[i]);
		if(commands.size()==0) return "Couldn't get commands.";
		Track emptyTrack;
		tracks.push_back(emptyTrack);
		for(unsigned j=0; j<commands.size(); j++){//for all commands
			ticks+=commands[j].first;//add delta to our current absolute time
			//deal with command based on its type
			Event temp;
			if((commands[j].second[0]&0xf0)==0x90&&commands[j].second[2]){//Note on
				temp.duration=0;//find the corresponding note off event to set duration
				for(unsigned k=j+1; k<commands.size(); k++){
					temp.duration+=commands[k].first;
					if(//Note off
						((commands[k].second[0]&0xf0)==0x90&&commands[k].second[2]==0)
						||
						(commands[k].second[0]&0xf0)==0x80
					)
						//if same note as the note on
						if(commands[k].second[1]==commands[j].second[1]){
							temp.velocityUp=commands[k].second[2];//get the velocity
							break;//stop increasing the duration
						}
				}
				temp.type=Midi::Event::NOTE;
				temp.timeInTicks=ticks;
				temp.channel=commands[j].second[0]&0x0f;
				temp.note=commands[j].second[1];
				temp.velocityDown=commands[j].second[2];
				tracks.back().push_back(temp);
			}
			else if((commands[j].second[0]&0xf0)==0xc0){//Voice
				temp.type=Midi::Event::VOICE;
				temp.timeInTicks=ticks;
				temp.channel=commands[j].second[0]&0x0f;
				temp.voice=commands[j].second[1];
				tracks.back().push_back(temp);
			}
			//if it's a meta event
			else if(commands[j].second[0]==(unsigned char)0xff){
				//figure out what type it is, then fill in the fields accordingly
				if(commands[j].second[1]==0x51){//Tempo
					temp.type=Midi::Event::TEMPO;
					temp.timeInTicks=ticks;
					temp.usPerQuarter=bToU(commands[j].second.begin()+3, 3);
					tracks.back().push_back(temp);
				}
				else if(commands[j].second[1]==0x58){//Time signature
					temp.type=Midi::Event::TIME;
					temp.timeInTicks=ticks;
					temp.timeSigTop=commands[j].second[3];
					//The bottom number is never not a power of 2, so log2(bottom)
					//is stored instead. But we don't have the same needs, so convert.
					temp.timeSigBottom=1<<commands[j].second[4];
					tracks.back().push_back(temp);
				}
				else if(commands[j].second[1]==0x59){//Key signature
					int sharps=(signed char)commands[j].second[3];
					temp.type=Midi::Event::KEY;
					temp.timeInTicks=ticks;
					temp.sharps=sharps;
					temp.minor=commands[j].second[4]!=0;//1 is minor, 0 is major
					tracks.back().push_back(temp);
				}
				else if(commands[j].second[1]==0x01){//Text
					temp.type=Midi::Event::TEXT;
					temp.timeInTicks=ticks;
					for(int k=0; k<commands[j].second[2]; ++k)
						temp.text.push_back(commands[j].second[3+k]);
					tracks.back().push_back(temp);
				}
			}//end of if meta event
		}//end of for all commands
	}//end of for all chunks
	return "";
}

/*=====Skeleton=====*/
/*-----Component-----*/
Component& Component::operator>>(Component& other){
	addOutput(other);
	other.addInput(*this);
	return other;
}

/*-----System-----*/
System::System(unsigned sampleRate, unsigned samplesAtOnce):
	sampleRate(sampleRate),
	samplesAtOnce(samplesAtOnce)
{}

System::~System(){
	for(unsigned i=0; i<components.size(); ++i) delete components[i];
}

void System::addComponent(std::string name, Component* component){
	components.push_back(component);
	componentsByName[name]=component;
	component->initialize(sampleRate, samplesAtOnce);
}

Component& System::component(std::string name){ return *componentsByName[name]; }

void System::attachToOutput(Component& component){
	samples=(float*)component.perform("samples", NULL);
}

const float* System::evaluate(){
	for(unsigned i=0; i<components.size(); ++i) components[i]->evaluate();
	return samples;
}

/*=====Controllers=====*/
/*-----Notes-----*/
void Notes::loadFromMidi(string fileName){
	midi.read(fileName);
	for(unsigned i=0; i<midi.tracks[0].size();){
		if(midi.tracks[0][i].type!=Midi::Event::TEMPO)
			midi.tracks[0].erase(midi.tracks[0].begin()+i);
		else ++i;
	}
	places.resize(midi.tracks.size(), 0);
}

void Notes::initialize(unsigned _sampleRate, unsigned _samplesAtOnce){
	sampleRate=_sampleRate;
	samplesAtOnce=_samplesAtOnce;
	tick=0;
	ticksPerSample=float(midi.ticksPerQuarter)/sampleRate;
	for(unsigned i=0; i<midi.tracks[0].size(); ++i){
		if(midi.tracks[0][i].timeInTicks>0) break;
		if(midi.tracks[0][i].type==Midi::Event::TEMPO)
			ticksPerSample=
				float(midi.ticksPerQuarter)/sampleRate/
				(midi.tracks[0][i].usPerQuarter/1e6f);
		++places[0];
	}
}

void Notes::addOutput(Component& output){
	outputs.push_back((Delegate*)output.perform("delegate", NULL));
}

void Notes::evaluate(){
	float destination=tick+samplesAtOnce*ticksPerSample;
	while(true){
		if(places[0]<midi.tracks[0].size()&&midi.tracks[0][places[0]].timeInTicks<destination){
			playTo((float)midi.tracks[0][places[0]].timeInTicks);
			if(midi.tracks[0][places[0]].type==Midi::Event::TEMPO)
				ticksPerSample=
					1.0f*midi.ticksPerQuarter/sampleRate/
					(midi.tracks[0][places[0]].usPerQuarter/1e6f);
			++places[0];
		}
		else{
			playTo(destination);
			break;
		}
	}
	tick=destination;
}

void Notes::playTo(float destination){
	for(unsigned i=0; i<outputs.size(); ++i)
		while(places[i+1]<midi.tracks[i+1].size()&&midi.tracks[i+1][places[i+1]].timeInTicks<destination){
			if(midi.tracks[i+1][places[i+1]].type==Midi::Event::NOTE){
				outputs[i]->note(
					440*pow(2.0f, (midi.tracks[i+1][places[i+1]].note-69)/12.0f),
					unsigned(midi.tracks[i+1][places[i+1]].duration/ticksPerSample),
					midi.tracks[i+1][places[i+1]].velocityDown/127.0f,
					int((destination-tick)/ticksPerSample));
			}
			++places[i+1];
		}
}

/*=====Sources=====*/
/*-----LFSRNoise-----*/
LFSRNoise::LFSRNoise(int decayLength): decayLength(decayLength) {}

LFSRNoise::~LFSRNoise(){ delete samples; }

void* LFSRNoise::perform(string action, void* data){
	if(action=="iv") volume=*(float*)data;
	else if(action=="volume") desiredVolume=*(float*)data;
	else if(action=="samples") return samples;
	return NULL;
}

void LFSRNoise::initialize(unsigned sampleRate, unsigned samplesAtOnce){
	samples=new float[samplesAtOnce];
	size=samplesAtOnce;
	state=1;
	volume=0.0f;
	desiredVolume=0.0f;
}

void LFSRNoise::evaluate(){
	volume=(volume*decayLength+desiredVolume)/(decayLength+1);
	for(unsigned i=0; i<size; ++i){
		state=(
			(state<<1)|
			(
				((state&0x8000)>>15)^
				((state&0x2000)>>13)^
				((state&0x1000)>>12)^
				((state&0x0400)>>10)
			)
		)&0xffff;
		samples[i]=volume*(2.0f*state/0xffff-1);
	}
}

/*-----Sonic-----*/
Sonic::Sonic(float volume): volume(volume), desiredVolume(volume) {
	notesDelegate.oscillators=oscillators;
	notesDelegate.notes=&notes;
}

Sonic::~Sonic(){ delete samples; }

void* Sonic::perform(string action, void* data){
	if(action=="volume") desiredVolume=*(float*)data;
	else if(action=="samples") return samples;
	else if(action=="delegate") return (Delegate*)&notesDelegate;
	return NULL;
}

void Sonic::setOscillator(
	unsigned oscillator, float frequencyMultiplier, float amplitude, float attack, float decay, float sustain, float release
){
	oscillators[oscillator].frequencyMultiplier=frequencyMultiplier;
	oscillators[oscillator].amplitude=amplitude;
	oscillators[oscillator].attack=attack;
	oscillators[oscillator].decay=decay;
	oscillators[oscillator].sustain=sustain;
	oscillators[oscillator].release=release;
}

void Sonic::connectOscillators(unsigned from, unsigned to, float amount){
	oscillators[to].inputs[from]=amount;
}

void Sonic::connectToOutput(unsigned oscillator){
	oscillators[oscillator].output=true;
}

void Sonic::initialize(unsigned sampleRate, unsigned samplesAtOnce){
	samples=new float[samplesAtOnce];
	size=samplesAtOnce;
	notesDelegate.sampleRate=sampleRate;
}

void Sonic::evaluate(){
	volume=(volume*31+desiredVolume)/32;
	for(unsigned i=0; i<size; ++i){
		samples[i]=0.0f;
		for(unsigned j=0; j<notes.size();){
			++notes[j].age;
			if(notes[j].age<0){
				++j;
				continue;
			}
			else if(notes[j].age==notes[j].duration)
				for(unsigned k=0; k<OSCILLATORS; ++k)
					notes[j].runners[k].stage=Runner::RELEASE;
			bool done=true;
			for(unsigned k=0; k<OSCILLATORS; ++k){
				notes[j].runners[k].phase+=notes[j].runners[k].step;
				switch(notes[j].runners[k].stage){
					case Runner::ATTACK:
						notes[j].runners[k].amplitude+=oscillators[k].attack;
						if(notes[j].runners[k].amplitude>1){
							notes[j].runners[k].amplitude=1;
							notes[j].runners[k].stage=Runner::DECAY;
						}
						if(oscillators[k].output) done=false;
						break;
					case Runner::DECAY:
						notes[j].runners[k].amplitude-=oscillators[k].decay;
						if(notes[j].runners[k].amplitude<oscillators[k].sustain){
							notes[j].runners[k].amplitude=oscillators[k].sustain;
							notes[j].runners[k].stage=Runner::SUSTAIN;
						}
						if(oscillators[k].output) done=false;
						break;
					case Runner::SUSTAIN:
						if(oscillators[k].output) done=false;
						break;
					case Runner::RELEASE:
						notes[j].runners[k].amplitude-=oscillators[k].release;
						if(notes[j].runners[k].amplitude<0) notes[j].runners[k].amplitude=0;
						else if(oscillators[k].output) done=false;
						break;
					default: break;
				}
				float modulatedPhase=notes[j].runners[k].phase;
				for(unsigned l=0; l<OSCILLATORS; ++l)
					modulatedPhase+=notes[j].runners[l].output*oscillators[k].inputs[l];
				notes[j].runners[k].output=wave(modulatedPhase)*notes[j].runners[k].amplitude*oscillators[k].amplitude;
				notes[j].runners[k].phase-=floor(notes[j].runners[k].phase);
				if(oscillators[k].output) samples[i]+=notes[j].runners[k].output*notes[j].volume*volume;
			}
			if(done){
				notes[j]=notes.back();
				notes.pop_back();
			}
			else ++j;
		}
	}
}

float Sonic::wave(float phase){
	phase-=floor(phase);
	return phase*(phase-0.5f)*(phase-1.0f)*20.784f;
}

Sonic::Oscillator::Oscillator(): output(false) {
	for(unsigned i=0; i<OSCILLATORS; ++i) inputs[i]=0.0f;
}

Sonic::Runner::Runner(): stage(ATTACK), phase(0), amplitude(0), output(0) {}

void Sonic::Delegate::note(float frequency, unsigned duration, float volume, unsigned wait){
	Note note;
	note.age=-(int)wait;
	note.volume=volume;
	note.duration=duration;
	for(unsigned i=0; i<OSCILLATORS; ++i)
		note.runners[i].step=frequency/sampleRate*oscillators[i].frequencyMultiplier;
	notes->push_back(note);
}

/*=====RisingTone=====*/
RisingTone::~RisingTone(){ delete[] samples; }

void* RisingTone::perform(std::string action, void* data){
	if(action=="samples") return samples;
	else if(action=="play"){
		freq=((float*)data)[0];
		maxVolume=((float*)data)[1];
		volume=0.0f;
		age=0;
		return NULL;
	}
	return NULL;
}

void RisingTone::initialize(unsigned _sampleRate, unsigned samplesAtOnce){
	sampleRate=_sampleRate;
	samples=new float[samplesAtOnce];
	size=samplesAtOnce;
	phase=0.0f;
	volume=0;
	age=sampleRate*2;
}

void RisingTone::evaluate(){
	for(unsigned i=0; i<size; ++i){
		if(age<(int)sampleRate/10) volume+=maxVolume*10.0f/sampleRate;
		else if(volume<=0) volume=0.0f;
		else volume-=maxVolume*3.0f/sampleRate;
		phase+=freq/sampleRate;
		phase-=floor(phase);
		freq+=120.0f/sampleRate;
		++age;
		samples[i]=volume*(phase-0.5f)/2;
	}
}

/*=====Processors=====*/
/*-----FastLowPass-----*/
FastLowPass::FastLowPass(float lowness): lowness(lowness) {}
FastLowPass::~FastLowPass(){ delete outputSamples; }

void* FastLowPass::perform(string action, void* data){
	if(action=="samples") return outputSamples;
	return NULL;
}

void FastLowPass::initialize(unsigned sampleRate, unsigned samplesAtOnce){
	outputSamples=new float[samplesAtOnce];
	size=samplesAtOnce;
	current=0;
}

void FastLowPass::addInput(Component& input){
	inputSamples=(float*)input.perform("samples", NULL);
}

void FastLowPass::evaluate(){
	for(unsigned i=0; i<size; ++i){
		current=(1-lowness)*inputSamples[i]+lowness*current;
		outputSamples[i]=current;
	}
}

/*-----Adder-----*/
Adder::~Adder(){ delete samples; }

void* Adder::perform(std::string action, void* data){
	if(action=="samples") return samples;
	else if(action=="volume") volume=*(float*)data;
	return NULL;
}

void Adder::initialize(unsigned sampleRate, unsigned samplesAtOnce){
	samples=new float[samplesAtOnce];
	for(unsigned i=0; i<samplesAtOnce; ++i) samples[i]=0.0f;
	size=samplesAtOnce;
}

void Adder::addInput(Component& input){
	inputs.push_back((float*)input.perform("samples", NULL));
}

void Adder::evaluate(){
	for(unsigned i=0; i<size; ++i) samples[i]=0;
	for(unsigned i=0; i<inputs.size(); ++i)
		for(unsigned j=0; j<size; ++j)
			samples[j]+=inputs[i][j]*volume;
	for(unsigned i=0; i<size; ++i){
		if(samples[i]<-1.0f) samples[i]=-1.0f;
		else if(samples[i]>1.0f) samples[i]=1.0f;
	}
}
