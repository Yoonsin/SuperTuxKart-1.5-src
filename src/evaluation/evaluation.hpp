#pragma once

#include <vector>
#include <string>
#include "utils/vec3.hpp"

#include <irrlicht.h>

struct WorldRecordPoint
{
	float time;
	Vec3 pos;
};
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

class Evaluation
{
private:
	std::vector<WorldRecordPoint> m_wr_point;
	std::vector<StandardPoint> m_st_point;
	Result m_result;
	float m_current_score;
	float m_time_offset;
	int m_wr_index;
	int m_st_index;

	Evaluation() : m_current_score(100.0f), m_time_offset(0.0f), m_wr_index(0), m_st_index(0), m_result{false, 0.0f} {}
	Result getDistance(const Vec3& Player, const Vec3& wr_A, const Vec3& wr_B, const Vec3& st_A, const Vec3& st_B);

public:
	static Evaluation* get()
	{
		static Evaluation instance;
		return &instance;
	}
	void reset() { m_current_score = 100.0f; }
	float getScore() const { return m_current_score; }
	void update(const Vec3& kart_pos);
	void loadWorldRecordCSV(const std::string& filename);

	//추가
	void render();

};