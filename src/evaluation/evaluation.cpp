#pragma once
#include "evaluation.hpp"

#include <fstream>
#include <sstream>
#include <numeric>
#include <algorithm>

#include <SMesh.h>
#include <SMeshBuffer.h>
#include <IMeshSceneNode.h>
#include <IVideoDriver.h>

#include "utils/log.hpp"
#include "graphics/irr_driver.hpp"
#include "graphics/central_settings.hpp"
#include "tracks/terrain_info.hpp"

#include "graphics/sp/sp_base.hpp"
#include "graphics/sp/sp_mesh.hpp"
#include "graphics/sp/sp_mesh_node.hpp"

void Evaluation::loadWorldRecordCSV(const std::string& filename)
{
	m_wr_point.clear();
	std::ifstream file(filename);

	if (!file.is_open())
	{
		Log::error("Failed to open world record CSV file: %s", filename.c_str());
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
	Log::info("Evaluation", " 좌표 로드 완료 %d", m_wr_point.size());
}

Result Evaluation::getDistance(const Vec3& Player, const Vec3& wr_A, const Vec3& wr_B, const Vec3& st_A, const Vec3& st_B)
{
	float wr_A_y = wr_A.getY();
	float st_A_y = st_A.getY();
	float p_y = Player.getY();
	/*
	if(p_y - wr_A_y > 0.3f || wr_A_y - p_y > 0.3f)
	{
		return { true, 0.01 };
	}
	*/
	if (p_y - st_A_y > 0.5f || st_A_y - p_y > 0.5f)
	{
		return { true, 0.2 };
	}
	/*
	Vec3 LineAB = wr_B - wr_A;
	float dot_AB = LineAB.dot(LineAB);
	if(dot_AB == 0.0f || LineAB.length() < 0.001f)
	{
		float distance = (wr_A - Player).length();
		if (distance > 2.5f)
		{
			//대략적으로 0.2점 감점, 추후에 변경 가능
			return { true, 0.01 };
		}
		//통과
		return { false, 0.0 };
	}
	*/
	Vec3 LineAB = st_B - st_A;
	float dot_AB = LineAB.dot(LineAB);
	if (dot_AB == 0.0f || LineAB.length() < 0.001f)
	{
		float distance = (st_A - Player).length();
		if (distance > 1.8f)
		{
			//대략적으로 0.2점 감점, 추후에 변경 가능
			return { true, 0.2 };
		}
		//통과
		return { false, 0.0 };
	}
	/*
	Vec3 LineAP = Player - wr_A;
	float dot_AP_AB = LineAP.dot(LineAB);
	//여기는 우선 냅둠
	if ((LineAB - dot_AP_AB).length() < 0.0f)
	{

	}
	*/
	Vec3 LineAP = Player - st_A;
	float dot_AP_AB = LineAP.dot(LineAB);
	//여기는 우선 냅둠
	if ((LineAB - dot_AP_AB).length() < 0.0f)
	{

	}
	/*
	Vec3 LineAA = (wr_A - st_A);
	//라인 보정
	if (LineAA.length() <= 2.4f || LineAA.length() > 5.0f)
	{
		float distance = 2.5f;
		if((wr_A - Player).length() > distance)
		{
			return { true, 0.01 };
		}
		return { false, 0.0 };
	}
	
	float dot_AP_AA = LineAP.dot(LineAA);
	if(dot_AP_AA < 0.0f)
	{
		if(LineAP.length() > 2.5f)
		{
			return { true, 0.01 };
		}
	}
	
	if(dot_AP_AA - LineAA.length() > 0.0f)
	{
		if(LineAP.length() > 2.5f)
		{
			return { true, 0.01 };
		}
	}
	*/
	return { false, 0.0 };
}

void Evaluation::update(const Vec3& Player)
{
	if(m_wr_point.empty() || m_st_point.empty())
	{
		return;
	}
	/*
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
	*/
	int search_index = 200;
	int start_st = std::max(0, m_st_index - search_index);
	int end_st = std::min((int)m_st_point.size() - 1, m_st_index + search_index);
	float min_distance = 500;
	for (int i = start_st; i <= end_st; i++)
	{
		float distance = (Player - m_st_point[i].pos).length();
		if (distance < min_distance)
		{
			min_distance = distance;
			m_st_index = i;
		}
	}

	Vec3 wr_A = m_wr_point[m_st_index].pos;
	Vec3 wr_B = wr_A;
	Vec3 st_A = m_st_point[m_st_index].pos;
	Vec3 st_B = st_A;

	if (m_st_index < (int)m_st_point.size() - 1)
	{
		wr_B = m_wr_point[m_st_index + 1].pos;
		st_B = m_st_point[m_st_index + 1].pos;
	}
	else if (m_st_index > 0)
	{
		wr_A = m_wr_point[m_st_index - 1].pos;
		wr_B = m_wr_point[m_st_index].pos;
		st_A = m_st_point[m_st_index - 1].pos;
		st_B = m_st_point[m_st_index].pos;
	}

	m_result = getDistance(Player, wr_A, wr_B, st_A, st_B);

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

void Evaluation::render()
{
	if (m_wr_point.size() < 2 || m_st_point.size() < 2)
	{
		return;
	}
	if (!irr_driver)
	{
		return;
	}
	irr::scene::ISceneManager* smgr = irr_driver->getSceneManager();
	if (!smgr)
	{
		return;
	}

	irr::scene::SMeshBuffer* buffer = new irr::scene::SMeshBuffer();
	buffer->Material.Lighting = false;
	buffer->Material.BackfaceCulling = false;

	const irr::video::SColor lineColor(100, 255, 0, 0);

	const float width = 1.8f;
	const float lift = 0.5f;

	for (size_t i = 0; i < m_st_point.size(); ++i)
	{
		const Vec3 pos = m_st_point[i].pos;

		TerrainInfo terrain;
		terrain.update(pos + Vec3(0.0f, 5.0f, 0.0f));

		Vec3 surface(terrain.getHitPoint());
		Vec3 normal = terrain.getNormal();

		if (normal.length() < 0.001f)
		{
			surface = pos;
			normal = Vec3(0.0f, 1.0f, 0.0f);
		}
		else
		{
			normal.normalize();
		}

		Vec3 direction;
		if (i + 1 < m_st_point.size())
		{
			direction = m_st_point[i + 1].pos - pos;
		}
		else
		{
			direction = pos - m_st_point[i - 1].pos;
		}

		direction -= normal * direction.dot(normal);

		if (direction.length() < 0.001f)
		{
			continue;
		}

		direction.normalize();

		Vec3 right = direction.cross(normal);

		if (right.length() < 0.001f)
		{
			continue;
		}
		right.normalize();

		const Vec3 center = surface + normal * lift;
		const Vec3 left_pos = center - right * width;
		const Vec3 right_pos = center + right * width;

		buffer->Vertices.push_back(irr::video::S3DVertex(left_pos.getX(), left_pos.getY(), left_pos.getZ(), normal.getX(), normal.getY(), normal.getZ(), lineColor, 0, 0));
		buffer->Vertices.push_back(irr::video::S3DVertex(right_pos.getX(), right_pos.getY(), right_pos.getZ(), normal.getX(), normal.getY(), normal.getZ(), lineColor, 1, 0));
	}

	Log::info("Evaluation", "Building vertices: %d",buffer->Vertices.size());

	const irr::u32 segment_count = buffer->Vertices.size() / 2;
	for (irr::u32 i = 0; i + 1 < segment_count; ++i)
	{
		const irr::u32 idx = i * 2;
		buffer->Indices.push_back(idx);
		buffer->Indices.push_back(idx + 2);
		buffer->Indices.push_back(idx + 1);
		buffer->Indices.push_back(idx + 1);
		buffer->Indices.push_back(idx + 2);
		buffer->Indices.push_back(idx + 3);
	}

	buffer->recalculateBoundingBox();
	irr::scene::SMesh* mesh = new irr::scene::SMesh();
	mesh->addMeshBuffer(buffer);
	mesh->recalculateBoundingBox();

	irr::scene::ISceneNode* node = nullptr;
    bool using_spmesh = false;
#ifndef SERVER_ONLY
    if (CVS->isGLSL())
    {
        Log::info("Evaluation", "Converting route mesh to SPMesh");
        SP::SPMesh* spm = SP::convertEVTStandard(mesh, &lineColor);
        if (spm)
        {
            SP::SPMeshNode* spmn = new SP::SPMeshNode(
                spm, smgr->getRootSceneNode(), smgr, -1, "evaluation_route");
            spmn->setMesh(spm);
            spm->drop();
            node = spmn;
            spmn->drop();
            using_spmesh = true;
        }
    } 
    else
#endif
    {
        node = smgr->addMeshSceneNode(mesh);
    }

    Log::info("Evaluation", "Route node: %s", node ? "created" : "null");
    if (node && !using_spmesh)
    {
        node->setMaterialFlag(irr::video::EMF_LIGHTING, false);
        node->setMaterialFlag(irr::video::EMF_BACK_FACE_CULLING, false);
        node->setMaterialFlag(irr::video::EMF_ZWRITE_ENABLE, true);
        node->setAutomaticCulling(irr::scene::EAC_OFF);
    }

    if (!using_spmesh)
    {
        buffer->drop();
        mesh->drop();
    }
}