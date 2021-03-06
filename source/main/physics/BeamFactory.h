/*
This source file is part of Rigs of Rods
Copyright 2005-2012 Pierre-Michel Ricordel
Copyright 2007-2012 Thomas Fischer

For more information, see http://www.rigsofrods.com/

Rigs of Rods is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License version 3, as
published by the Free Software Foundation.

Rigs of Rods is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Rigs of Rods.  If not, see <http://www.gnu.org/licenses/>.
*/

// created by Thomas Fischer thomas{AT}thomasfischer{DOT}biz, 24th of August 2009

#ifndef __BeamFactory_H_
#define __BeamFactory_H_

#include "RoRPrerequisites.h"

#include "Beam.h"
#include "StreamableFactory.h"
#include "TwoDReplay.h"

#include <pthread.h>

class BeamFactory : public StreamableFactory < BeamFactory, Beam >
{
	friend class Network;
	friend class RoRFrameListener;
public:
	BeamFactory(Ogre::SceneManager *manager, Ogre::SceneNode *parent, Ogre::RenderWindow* win, Network *net, float *mapsizex, float *mapsizez, Collisions *icollisions, HeightFinder *mfinder, Water *w, Ogre::Camera *pcam);
	~BeamFactory();

	Beam *createLocal(int slotid);
	Beam *createLocal(Ogre::Vector3 pos, Ogre::Quaternion rot, Ogre::String fname, collision_box_t *spawnbox=NULL, bool ismachine=false, int flareMode=0, std::vector<Ogre::String> *truckconfig=0, Skin *skin=0, bool freePosition=false);
	Beam *createRemoteInstance(stream_reg_t *reg);

	Beam *getBeam(int source_id, int stream_id); // used by character

	Beam *getCurrentTruck() { return (current_truck<0)?0:trucks[current_truck]; };
	Beam *getTruck(int number) { return trucks[number]; };
	Beam **getTrucks() { return trucks; };
	int getCurrentTruckNumber() { return current_truck; };
	int getTruckCount() { return free_truck; };

	void setCurrentTruck(int new_truck);

	bool removeBeam(Beam *b);
	void removeCurrentTruck();
	void removeTruck(Collisions *collisions, char* inst, char* box);
	void removeTruck(int truck);
	
	bool enterRescueTruck();
	void repairTruck(Collisions *collisions, char* inst, char* box, bool keepPosition=false);

	void updateVisual(float dt);
	void updateAI(float dt);

	inline unsigned long getPhysFrame() { return physFrame; };

	void calcPhysics(float dt);
	void recalcGravityMasses();

	int updateSimulation(float dt);

	bool checkForActive(int j, std::bitset<MAX_TRUCKS> &sleepyList);
	void activateAllTrucks();
	void recursiveActivation(int j);
	void sendAllTrucksSleeping();
	void checkSleepingState();

	void windowResized();

protected:
	Collisions *icollisions;
	HeightFinder *mfinder;
	Network *net;
	Ogre::Camera *pcam;
	Ogre::RenderWindow* win;
	Ogre::SceneManager *manager;
	Ogre::SceneNode *parent;
	Water *w;
	float *mapsizex, *mapsizez;
	
	Beam *trucks[MAX_TRUCKS];
	int free_truck;
	int current_truck;

	TwoDReplay *tdr;

	unsigned long physFrame;

	int getFreeTruckSlot();
	int findTruckInsideBox(Collisions *collisions, char* inst, char* box);

	// functions used by friends
	void netUserAttributesChanged(int source, int streamid);
	void localUserAttributesChanged(int newid);

	bool syncRemoteStreams();
	void updateGUI();
	void removeInstance(Beam *b);
	void removeInstance(stream_del_t *del);
	void _deleteTruck(Beam *b);
};

#endif // __BeamFactory_H_
