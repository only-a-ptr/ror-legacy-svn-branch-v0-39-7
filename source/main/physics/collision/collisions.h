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
#ifndef __Collisions_H_
#define __Collisions_H_

#include "RoRPrerequisites.h"

#include "BeamData.h" // for collision_box_t
#include "Ogre.h"

typedef struct _eventsource
{
	char instancename[256];
	char boxname[256];
	Ogre::SceneNode *snode;
	Ogre::Quaternion direction;
	int scripthandler;
	int cbox;
	bool enabled;
} eventsource_t;

typedef std::vector<int> cell_t;

class Landusemap;

class Collisions
{
public:

	enum SurfaceType {
		FX_NONE,
		FX_HARD,    // hard surface: rubber burning and sparks
		FX_DUSTY,   // dusty surface (with dust colour)
		FX_CLUMPY,  // throws clumps (e.g. snow, grass) with colour
		FX_PARTICLE
	};

	// these are absolute maximums per terrain
	static const int MAX_COLLISION_BOXES = 5000;
	static const int MAX_COLLISION_TRIS = 100000;

private:

	typedef struct _hash
	{
		unsigned int cellid;
		cell_t *cell;
	} hash_t;

	typedef struct _collision_tri
	{
		Ogre::Vector3 a;
		Ogre::Vector3 b;
		Ogre::Vector3 c;
		Ogre::Matrix3 forward;
		Ogre::Matrix3 reverse;
		ground_model_t* gm;
		bool enabled;
	} collision_tri_t;


	static const int LATEST_GROUND_MODEL_VERSION = 3;
	static const int MAX_EVENT_SOURCE = 500;

	// this is a power of two, change with caution
	static const int HASH_POWER = 20;
	static const int HASH_SIZE = 1 << HASH_POWER;

	// how many elements per cell? power of 2 minus 2 is better
	static const int CELL_BLOCKSIZE = 126;

	// how many cells in the pool? Increase in case of sparse distribution of objects
	//static const int MAX_CELLS = 10000;
	static const int UNUSED_CELLID = 0xFFFFFFFF;
	static const int UNUSED_CELLELEMENT = 0xFFFFFFFF;

	// terrain size is limited to 327km x 327km:
	static const int CELL_SIZE = 2.0; // we divide through this
	static const int MAXIMUM_CELL = 0x7FFF;

	// collision boxes pool
	collision_box_t collision_boxes[MAX_COLLISION_BOXES];
	collision_box_t *last_called_cbox;
	int free_collision_box;

	// collision tris pool;
	collision_tri_t *collision_tris;
	int free_collision_tri;

	// collision hashtable
	hash_t hashtable[HASH_SIZE];

	// cell pool
	std::vector<cell_t*> cells;

	// ground models
	std::map<Ogre::String, ground_model_t> ground_models;

	// event sources
	eventsource_t eventsources[MAX_EVENT_SOURCE];
	int free_eventsource;

	bool permitEvent(int filter);
	bool envokeScriptCallback(collision_box_t *cbox, node_t *node=0);

	HeightFinder *hfinder;
	Landusemap *landuse;
	Ogre::ManualObject *debugmo;
	Ogre::SceneManager *smgr;
	RoRFrameListener *mefl;
	bool debugMode;
	int collision_count;
	int collision_version;
	int largest_cellcount;
	long max_col_tris;
	unsigned int hashmask;

	void hash_add(int cell_x, int cell_z, int value);
	void hash_free(int cell_x, int cell_z, int value);
	cell_t *hash_find(int cell_x, int cell_z);
	unsigned int hashfunc(unsigned int cellid);
	void parseGroundConfig(Ogre::ConfigFile *cfg, Ogre::String groundModel=Ogre::String());

	Ogre::Vector3 calcCollidedSide(const Ogre::Vector3& pos, Ogre::Vector3& lo, Ogre::Vector3& hi);

public:

	bool forcecam;
	Ogre::Vector3 forcecampos;
	ground_model_t *defaultgm, *defaultgroundgm;

	eventsource_t *getEvent(int eventID) { return &eventsources[eventID]; };

	Collisions() {}; // for wrapper, DO NOT USE!

	Collisions(RoRFrameListener *efl, Ogre::SceneManager *mgr, bool debugMode);


	Ogre::Vector3 getPosition(char* instance, char* box);
	Ogre::Quaternion getDirection(char* instance, char* box);
	collision_box_t *getBox(char* instance, char* box);

	void setHfinder(HeightFinder *hf);

	eventsource_t *isTruckInEventBox(Beam *truck);

	bool collisionCorrect(Ogre::Vector3 *refpos);
	bool groundCollision(node_t *node, float dt, ground_model_t** gm, float *nso=0);
	bool isInside(Ogre::Vector3 pos, char* instance, char* box, float border=0);
	bool isInside(Ogre::Vector3 pos, collision_box_t *cbox, float border=0);
	bool nodeCollision(node_t *node, bool iscinecam, int contacted, float dt, float* nso, ground_model_t** ogm, int *handlernum=0);

	void clearEventCache();
	void finishLoadingTerrain();
	void primitiveCollision(node_t *node, Ogre::Vector3 &normal, Ogre::Vector3 &force, Ogre::Vector3 &velocity, float dt, ground_model_t* gm, float* nso, float penetration=0, float reaction=-1.0f);
	void printStats();

	int addCollisionBox(Ogre::SceneNode *tenode, bool rotating, bool virt, float px, float py, float pz, float rx, float ry, float rz, float lx, float hx, float ly, float hy, float lz, float hz, float srx, float sry, float srz, const char* eventname, const char* instancename, bool forcecam, Ogre::Vector3 campos, float scx=1.0f, float scy=1.0f, float scz=1.0f, float drx=0.0f, float dry=0.0f, float drz=0.0f, int event_filter=EVENT_ALL, int scripthandler=-1);
	int addCollisionMesh(Ogre::String meshname, Ogre::Vector3 pos, Ogre::Quaternion q, Ogre::Vector3 scale, ground_model_t *gm=0, std::vector<int> *collTris=0);
	int addCollisionTri(Ogre::Vector3 p1, Ogre::Vector3 p2, Ogre::Vector3 p3, ground_model_t* gm);
	int createCollisionDebugVisualization();
	int enableCollisionTri(int number, bool enable);
	int removeCollisionBox(int number);
	int removeCollisionTri(int number);

	// ground models things
	int loadDefaultModels();
	int loadGroundModelsConfigFile(Ogre::String filename);
	std::map<Ogre::String, ground_model_t> *getGroundModels() { return &ground_models; };
	void setupLandUse(const char *configfile);
	ground_model_t *getGroundModelByString(const Ogre::String name);
	ground_model_t *last_used_ground_model;

	void getMeshInformation(Ogre::Mesh* mesh, size_t &vertex_count, Ogre::Vector3* &vertices,
		size_t &index_count, unsigned* &indices,
		const Ogre::Vector3 &position = Ogre::Vector3::ZERO,
		const Ogre::Quaternion &orient = Ogre::Quaternion::IDENTITY, const Ogre::Vector3 &scale = Ogre::Vector3::UNIT_SCALE);
	void resizeMemory(long newSize);
};

#endif // __Collisions_H_
