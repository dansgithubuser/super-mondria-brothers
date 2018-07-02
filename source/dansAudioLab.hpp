#ifndef DANSAUDIOLAB_HPP_INCLUDED
#define DANSAUDIOLAB_HPP_INCLUDED

#include <vector>
#include <map>
#include <string>
#include <cstdlib>

namespace dal{

/*=====MIDI=====*/
class Midi{
	public:
		/*-----types-----*/
		class Event{
			public:
				//---constants---//
				enum Type{
					TEMPO,
					TIME,//time signature
					KEY,//key signature
					NOTE,//a note on and note off midi event pair
					VOICE,//instrument
					TEXT
				};
				//---functions---//
				//for key signature events
				//return frequency of lowest midi note that is root of the key
				float freq();
				//for key signature events
				//return the lowest midi note that is the root of the key
				unsigned root();
				//for time signature events
				//return the number of ticks per bar given ticks per quarter
				unsigned tpb(int tpq);
				//return the end time of the event in ticks
				int endTicks();
				//---variables---//
				//fields for all events
				Type type;//type of event
				int timeInTicks;//when it occurs
				int channel;//what channel it occurs on
				union{
					//fields for tempo events
					int usPerQuarter;//microseconds per quarter note
					//fields for time signature events
					struct{ int timeSigTop, timeSigBottom; };
					//fields for key signature events
					struct{
						int sharps;//sharps in the key; negative for flats
						bool minor;//whether or not it's a minor key
					};
					//fields for note events
					struct{
						int duration;//duration of the note in ticks
						int note;//the note number
						int velocityDown;//how hard the note is hit
						int velocityUp;//how suddenly the note is released
					};
					//fields for voice events
					int voice;
				};
				//-ununionable fields-//
				//for text events
				std::vector<char> text;
		};
		typedef std::vector<Event> Track;
		/*-----functions-----*/
		std::string read(std::string filename);
		bool write(std::string filename);
		int getUsPerQuarter();
		/*-----variables-----*/
		int ticksPerQuarter;//from the MIDI header
		std::vector<Track> tracks;//all the tracks in the file
	private:
		std::string parse(std::vector<unsigned char>& bytes);
};

/*=====Skeleton=====*/
class System;

class Component{
	friend class System;
	public:
		virtual ~Component(){}
		Component& operator>>(Component& other);
		virtual void* perform(std::string action, void* data){ return NULL; }
	private:
		virtual void initialize(unsigned sampleRate, unsigned samplesAtOnce){}
		virtual void addInput(Component& input){}
		virtual void addOutput(Component& output){}
		virtual void evaluate()=0;
};

class System{
	public:
		System(unsigned sampleRate, unsigned samplesAtOnce);
		~System();
		void addComponent(std::string name, Component*);
		Component& component(std::string name);
		void attachToOutput(Component&);
		const float* evaluate();
	private:
		std::vector<Component*> components;
		std::map<std::string, Component*> componentsByName;
		unsigned sampleRate, samplesAtOnce;
		float* samples;
};

/*=====Controllers=====*/
class Notes: public Component{
	public:
		class Delegate{
			public:
				virtual void note(
					float frequency, unsigned duration, float volume, unsigned wait
				)=0;
		};
		void loadFromMidi(std::string fileName);
	private:
		void initialize(unsigned sampleRate, unsigned samplesAtOnce);
		void addOutput(Component& output);
		void evaluate();
		void playTo(float destination);
		unsigned sampleRate, samplesAtOnce;
		float tick, ticksPerSample;
		std::vector<Delegate*> outputs;
		std::vector<unsigned> places;
		Midi midi;
};

/*=====Sources=====*/
class LFSRNoise: public Component{
	public:
		LFSRNoise(int decayLength);
		~LFSRNoise();
		void* perform(std::string action, void* data);
	private:
		void initialize(unsigned sampleRate, unsigned samplesAtOnce);
		void evaluate();
		float* samples;
		unsigned size, state;
		float desiredVolume, volume;
		int decayLength;
};

float triangle(float phase);

class Noter: public Component{
	public:
		Noter(std::vector<std::vector<std::pair<float, int> > > notes):
			notes(notes), volume(0.0f), desiredVolume(0.0f), phase(0.0f), note(0), noteSet(0)
		{}
		~Noter(){ delete samples; }

		void* perform(std::string action, void* data){
			if(action=="samples") return samples;
			t=0;
			note=0;
			done=false;
			desiredVolume=*(float*)data;
			noteSet=std::rand()%notes.size();
			return NULL;
		}

	private:
		void initialize(unsigned sampleRate, unsigned samplesAtOnce){
			samples=new float[samplesAtOnce];
			size=samplesAtOnce;
		}

		void evaluate(){
			for(unsigned i=0; i<size; ++i){
				if(done) desiredVolume=0.0f;
				samples[i]=volume*triangle(phase);
				phase+=notes[noteSet][note].first;
				phase-=int(phase);
				++t;
				if(t>notes[noteSet][note].second){
					t=0;
					if(note<notes[noteSet].size()-1) ++note;
					else done=true;
				}
				volume=(8*volume+desiredVolume)/9;
			}
		}

		float* samples;
		unsigned size;
		std::vector<std::vector<std::pair<float, int> > > notes;
		int t;
		float phase;
		int note;
		bool done;
		float volume;
		float desiredVolume;
		int noteSet;
};

class Sonic: public Component{
	public:
		Sonic(float volume);
		~Sonic();
		void* perform(std::string action, void* data);
		void setOscillator(
			unsigned oscillator, float frequencyMultiplier, float amplitude,
			float attack, float decay, float sustain, float release
		);
		void connectOscillators(unsigned from, unsigned to, float amount);
		void connectToOutput(unsigned oscillator);
	private:
		static const unsigned OSCILLATORS=4;
		void initialize(unsigned sampleRate, unsigned samplesAtOnce);
		void evaluate();
		float wave(float phase);
		float* samples;
		unsigned size;
		float volume, desiredVolume;
		struct Oscillator{
			Oscillator();
			float attack, decay, sustain, release;
			float frequencyMultiplier, amplitude, inputs[OSCILLATORS];
			bool output;
		};
		Oscillator oscillators[OSCILLATORS];
		struct Runner{
			Runner();
			enum Stage{ ATTACK, DECAY, SUSTAIN, RELEASE };
			Stage stage;
			float phase, step, amplitude, output;
		};
		struct Note{
			Runner runners[OSCILLATORS];
			float volume;
			int age, duration;
		};
		std::vector<Note> notes;
		class Delegate: public Notes::Delegate{
			public:
				void note(
					float frequency, unsigned duration, float volume, unsigned wait
				);
				Oscillator* oscillators;
				std::vector<Note>* notes;
				unsigned sampleRate;
		} notesDelegate;
};

class RisingTone: public Component{
	public:
		~RisingTone();
		void* perform(std::string action, void* data);
	private:
		void initialize(unsigned sampleRate, unsigned samplesAtOnce);
		void evaluate();
		float* samples;
		unsigned size, sampleRate;
		float phase, freq, volume, maxVolume;
		int age;
};

class MidiOut: public Component{
};

/*=====Processors=====*/
class FastLowPass: public Component{
	public:
		FastLowPass(float lowness);
		~FastLowPass();
		void* perform(std::string action, void* data);
	private:
		void initialize(unsigned sampleRate, unsigned samplesAtOnce);
		void addInput(Component& input);
		void evaluate();
		float* inputSamples;
		float* outputSamples;
		float lowness, current;
		unsigned size;
};

class Adder: public Component{
	public:
		Adder(): volume(1.0f) {}
		~Adder();
		void* perform(std::string action, void* data);
	private:
		void initialize(unsigned sampleRate, unsigned samplesAtOnce);
		void addInput(Component& input);
		void evaluate();
		std::vector<float*> inputs;
		float* samples;
		unsigned size;
		float volume;
};

}//namespace dal

#endif
