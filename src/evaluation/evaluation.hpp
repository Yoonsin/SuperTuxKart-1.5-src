#pragma once

#include <vector>
#include <string>
#include "utils/vec3.hpp"

#include <irrlicht.h>

struct StandardPoint
{
	float time;
	Vec3 pos;
};
struct Result
{
	bool RouteDeviation;
	float SubScore;
};
class AbstractKart;

class Evaluation
{
private:
	std::vector<StandardPoint> m_st_point;

	float m_current_score;
	int m_st_index;
	unsigned int m_wall_collision_count = 0;
	unsigned int m_fall_rescue_count = 0;
	double m_outside_time = 0.0;
	double m_wall_contact_time = -1.0;
	bool m_csv_written = false;

	Evaluation() : m_current_score(100.0f), m_st_index(0),
	               m_active_route_chunk(-1) {}
	Result getDistance(const Vec3& Player, const Vec3& st_A);

	enum { ROUTE_CHUNK_POINTS = 100 };
	std::vector<irr::scene::ISceneNode*> m_route_chunks;
	int m_active_route_chunk;

	irr::scene::ISceneNode* createRouteChunk(size_t start, size_t end);
	void clearRouteChunks();
	void updateRouteVisibility();

public:
	static Evaluation* get()
	{
		static Evaluation instance;
		return &instance;
	}
	void reset();
	float getScore() const { return m_current_score; }
	void loadWorldRecordCSV(const std::string& filename);
	void loadTrackReference(const std::string& track_name);

	void render();
	void update(const Vec3& kart_pos, float elapsed_time);

	AbstractKart* getEvaluationKart() const;
	bool isEvaluatingKart(const AbstractKart* kart) const;
	bool isRecordingKart(const AbstractKart* kart) const;

	void onWallContact(double race_time);
	void onFallRescue();

	void recordcsv(float driving_time);
};