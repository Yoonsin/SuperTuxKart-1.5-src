#pragma once
#include "evaluation.hpp"

#include <fstream>
#include <sstream>
#include <numeric>
#include <algorithm>

#include "utils/log.hpp"

void Evaluation::loadWorldRecordCSV(const std::string& filename)
{
	m_wr_point.clear();
	std::ifstream file(filename);

	if (!file.is_open())
	{
		Log::error("Failed to open world record CSV file: {}", filename.c_str());
		return;
	}

	std::string line;
	std::getline(file, line); // Skip header line

	while (std::getline(file, line))
	{
		if(line.empty()) continue;

		std::istringstream ss(line);
		std::string token;
		WorldRecordPoint wr_point;
		StandardPoint st_point;
		float x, y, z;

		if (std::getline(ss, token, ','))
		{
			wr_point.time = std::stof(token);
		}
		if (std::getline(ss, token, ','))
		{
			x = std::stof(token);
		}
		if (std::getline(ss, token, ','))
		{
			y = std::stof(token);
		}
		if (std::getline(ss, token, ','))
		{
			z = std::stof(token);
		}

		wr_point.pos = Vec3(x, y, z);
		m_wr_point.push_back(wr_point);

		if (std::getline(ss, token, ','))
		{
			st_point.time = std::stof(token);
		}
		if (std::getline(ss, token, ','))
		{
			x = std::stof(token);
		}
		if (std::getline(ss, token, ','))
		{
			y = std::stof(token);
		}
		if (std::getline(ss, token, ','))
		{
			z = std::stof(token);
		}

		st_point.pos = Vec3(x, y, z);
		m_st_point.push_back(st_point);
	}
	Log::info("Evaluation", "성공적으로 %d개의 WR 좌표를 메모리에 로드했습니다!", m_wr_point.size());
}

Result Evaluation::getDistance(const Vec3& Player, const Vec3& wr_A, const Vec3& wr_B, const Vec3& st_A)
{
	float wr_A_y = wr_A.getY();
	float p_y = Player.getY();

	if(p_y - wr_A_y > 0.3f || wr_A_y - p_y > 0.3f)
	{
		return { true, 0.2 };
	}

	Vec3 LineAB = wr_B - wr_A;
	float dot_AB = LineAB.dot(LineAB);
	if(dot_AB == 0.0f || LineAB.length() < 0.001f)
	{
		float distance = (wr_A - Player).length();
		if (distance > 1.8f)
		{
			//대략적으로 0.2점 감점, 추후에 변경 가능
			return { true, 0.2 };
		}
		//통과
		return { false, 0.0 };
	}

	Vec3 LineAP = Player - wr_A;
	float dot_AP_AB = LineAP.dot(LineAB);
	//여기는 우선 냅둠
	if ((LineAB - dot_AP_AB).length() < 0.0f)
	{

	}

	Vec3 LineAA = (wr_A - st_A);
	//라인 보정
	if (LineAA.length() <= 1.2f || LineAA.length() > 4.0f)
	{
		float distance = 1.8f;
		if((wr_A - Player).length() > distance)
		{
			return { true, 0.2 };
		}
		return { false, 0.0 };
	}

	float dot_AP_AA = LineAP.dot(LineAA);
	if(dot_AP_AA < 0.0f)
	{
		if(LineAP.length() > 1.8f)
		{
			return { true, 0.2 };
		}
	}

	if(dot_AP_AA - LineAA.length() > 0.0f)
	{
		if(LineAP.length() > 1.8f)
		{
			return { true, 0.2 };
		}
	}

	return { false, 0.0 };
}

void Evaluation::update(float delta_time, const Vec3& Player)
{
	if(m_wr_point.empty() || m_st_point.empty())
	{
		return;
	}

	m_time_offset += delta_time;
	if(m_time_offset >= 0.1f)
	{
		m_time_offset = 0.0f;

		int search_index = 200;
		int start_wr = std::max(0, m_wr_index - search_index);
		int end_wr = std::min((int)m_wr_point.size() - 1, m_wr_index + search_index);
		float min_distance = 500;

		for(int i = start_wr; i <= end_wr; i++)
		{
			float distance = (Player - m_wr_point[i].pos).length();
			if(distance < min_distance)
			{
				min_distance = distance;
				m_wr_index = i;
			}
		}
	}

	Vec3 wr_A = m_wr_point[m_wr_index].pos;
	Vec3 wr_B = m_wr_point[m_wr_index + 1].pos;
	Vec3 st_A = m_st_point[m_wr_index].pos;

	m_result = getDistance(Player, wr_A, wr_B, st_A);

	if(m_result.RouteDeviation)
	{
		m_current_score -= m_result.SubScore;
		if(m_current_score < 0.0f)
		{
			m_current_score = 0.0f;

		}
		Log::info("Current Score", "%f", m_current_score);
	}
}