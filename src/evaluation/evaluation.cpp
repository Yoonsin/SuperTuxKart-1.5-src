#include "evaluation.hpp"
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <locale>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>

#include <SMesh.h>
#include <SMeshBuffer.h>
#include <IMeshSceneNode.h>

#include "utils/log.hpp"
#include "graphics/irr_driver.hpp"
#include "graphics/material_manager.hpp"
#include "graphics/central_settings.hpp"
#include "tracks/terrain_info.hpp"

#include "graphics/sp/sp_base.hpp"
#include "graphics/sp/sp_mesh.hpp"
#include "graphics/sp/sp_mesh_buffer.hpp"
#include "graphics/sp/sp_mesh_node.hpp"

#include "io/file_manager.hpp"

#include "config/user_config.hpp"
#include "karts/abstract_kart.hpp"
#include "modes/world.hpp"
#include "race/race_manager.hpp"
#include "tracks/track.hpp"
#include "utils/file_utils.hpp"
#include "karts/controller/ghost_controller.hpp"

void Evaluation::reset()
{
	//기존 스코어
	m_current_score = 100.0f;
	m_st_index = 0;

	//savecsv용
	m_wall_collision_count = 0;
	m_fall_rescue_count = 0;
	m_outside_time = 0.0;
	m_wall_contact_time = -1.0;
	m_csv_written = false;

	clearRouteChunks();
}

AbstractKart* Evaluation::getEvaluationKart() const
{
	World* world = World::getWorld();

	if (!world)
		return nullptr;

	if (RaceManager::get()->isWatchingReplay())
	{
		return world->getNumKarts() > 0 ? world->getKart(0) : nullptr;
	}

	return world->getLocalPlayerKart(0);
}

bool Evaluation::isEvaluatingKart(const AbstractKart* kart) const
{
	World* world = World::getWorld();

	if (!UserConfigParams::m_course_evaluation ||
		!RaceManager::get()->isTimeTrialMode() ||
		!world ||
		!kart ||
		kart != getEvaluationKart() ||
		!world->isActiveRacePhase() ||
		kart->hasFinishedRace() ||
		kart->isEliminated() ||
		m_csv_written)
	{
		return false;
	}

	if (RaceManager::get()->isWatchingReplay())
	{
		const GhostController* ghost = dynamic_cast<const GhostController*>(kart->getController());

		return ghost && !ghost->isReplayEnd();
	}

	return true;
}

bool Evaluation::isRecordingKart(const AbstractKart* kart) const
{
	// 실제 주행에서만 충돌·구조 횟수를 기록합니다.
	return !RaceManager::get()->isWatchingReplay() &&
		isEvaluatingKart(kart);
}

void Evaluation::loadTrackReference(
	const std::string& track_name)
{
	// 이전 트랙의 기준 좌표가 남지 않도록 초기화합니다.
	m_st_point.clear();
	m_st_index = 0;

	std::string csv_name;

	if (track_name == "hacienda")
	{
		csv_name = "hacienda_comparison.csv";
	}
	else if (track_name == "lighthouse")
	{
		csv_name = "lighthouse_comparison.csv";
	}
	else
	{
		return;
	}

#if defined(ANDROID) || defined(__ANDROID__)
	const std::string csv_path = file_manager->getUserConfigFile(csv_name);
#else
	const std::string csv_path = "../../../data/evaluation/" + csv_name;
#endif

	Log::info("Evaluation", "csv path='%s', exists=%d", csv_path.c_str(), (int)file_manager->fileExists(csv_path));

	loadWorldRecordCSV(csv_path);
}

void Evaluation::onWallContact(double race_time)
{
	if (m_wall_contact_time < 0.0 || race_time - m_wall_contact_time >= 0.5)
	{
		++m_wall_collision_count;
	}

	m_wall_contact_time = race_time;
}

void Evaluation::onFallRescue()
{
	++m_fall_rescue_count;
}

void Evaluation::loadWorldRecordCSV(const std::string& filename)
{
	m_st_point.clear();
	m_st_index = 0;

	std::ifstream file(filename);
	Log::info("Evaluation", "evaluation track='%s'", filename.c_str());
	if (!file.is_open())
	{
		Log::error("Evaluation", "Failed to open world record CSV file : % s", filename.c_str());
		return;
	}

	std::string line;
	std::getline(file, line); // Skip header line

	while (std::getline(file, line))
	{
		if (line.empty()) continue;

		std::istringstream ss(line);
		std::string token;
		StandardPoint st_point;
		float x, y, z;

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
	Log::info("Evaluation", " 좌표 로드 완료 %d", m_st_point.size());
}

Result Evaluation::getDistance(const Vec3& Player, const Vec3& st_A)
{
	float st_A_y = st_A.getY();
	float p_y = Player.getY();
	if (p_y - st_A_y > 1.25f || st_A_y - p_y > 1.25f)
	{
		Log::info("Evaluation", "Y축 차이");
		return { true, 0.1 };
	}
	float distanceA = (st_A - Player).length();
	if (distanceA > 2.5f)
	{
		return { true, 0.1 };
	}
	return { false, 0.0 };
}

void Evaluation::update(const Vec3& Player, float elapsed_time)
{
	if (m_st_point.empty())
	{
		return;
	}

	int search_index = 100;
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

	updateRouteVisibility();

	Vec3 st_A = m_st_point[m_st_index].pos;

	const Result result = getDistance(Player, st_A);

	if (result.RouteDeviation)
	{
		m_outside_time += elapsed_time;

		m_current_score -= result.SubScore;
		if (m_current_score < 0.0f)
		{
			m_current_score = 0.0f;
		}
		Log::info("Current Score", "%f", m_current_score);
	}
}

void Evaluation::recordcsv(float driving_time)
{
	/*
    // Replay evaluation may update scores, but must not write result CSVs.
    if (RaceManager::get()->isWatchingReplay() ||
        !UserConfigParams::m_course_evaluation || m_csv_written)
    {
        return;
    }
	*/
#if defined(ANDROID) || defined(__ANDROID__)
	const char* platform = "android";
	const std::string result_dir = file_manager->getUserConfigDir();

#elif defined(_WIN32) || defined(WIN32)
	const char* platform = "windows";
	const std::string result_dir = file_manager->getUserConfigDir();

#else
	const char* platform = "unknown";
	const std::string result_dir = file_manager->getUserConfigDir();
#endif

	const auto now = std::chrono::system_clock::now();
	const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
	const std::tm* local_time = std::localtime(&now_time);

	if (!local_time)
	{
		Log::error("Evaluation", "Could not get local time.");
		return;
	}

	char datetime[32];

	if (std::strftime(datetime, sizeof(datetime), "%Y%m%d_%H%M%S", local_time) == 0)
	{
		Log::error("Evaluation", "Could not format date/time.");
		return;
	}

	const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;

	const std::string track = Track::getCurrentTrack()->getIdent();

	std::ostringstream filename;
	filename.imbue(std::locale::classic());

	filename << datetime << std::setfill('0') << std::setw(3) << milliseconds << "_" << track << "_" << platform << ".csv";

	const std::string path = result_dir + filename.str();

	//const std::string path = file_manager->getUserConfigFile(filename.str());

	std::ostringstream csv;
	csv.imbue(std::locale::classic());

	csv << "driving time,wall collision," "fall rescue,outside time,total score\n";
	//m_current_score -= m_fall_rescue_count * 5.0f + m_wall_collision_count * 2.0f;
	csv << std::fixed << std::setprecision(3) << driving_time << "," << m_wall_collision_count << "," << m_fall_rescue_count << "," << m_outside_time << "," << m_current_score << "\n";

	FILE* file = FileUtils::fopenU8Path(path, "w");

	if (!file)
	{
		Log::error("Evaluation", "Could not open '%s'.", path.c_str());
		return;
	}

	const std::string content = csv.str();

	const bool written = std::fwrite(content.data(), 1, content.size(), file) == content.size();

	const int close_result = std::fclose(file);

	if (!written || close_result != 0)
	{
		Log::error("Evaluation", "Could not write '%s'.", path.c_str());
		return;
	}

	m_csv_written = true;

	Log::info("Evaluation", "Saved result CSV: '%s'.", path.c_str());
}

void Evaluation::render()
{
	clearRouteChunks();

	if (m_st_point.size() < 2 || !irr_driver)
	{
		return;
	}

	const size_t step = ROUTE_CHUNK_POINTS - 1;
	for (size_t start = 0; start + 1 < m_st_point.size(); start += step)
	{
		const size_t end = std::min(start + ROUTE_CHUNK_POINTS, m_st_point.size());

		// 다음 조각과 연결되도록 step이 chunk 크기보다 한 점 작습니다.
		m_route_chunks.push_back(createRouteChunk(start, end));
	}

	updateRouteVisibility();
}

irr::scene::ISceneNode* Evaluation::createRouteChunk(size_t start, size_t end)
{
	if (!irr_driver)
	{
		return nullptr;
	}

	irr::scene::ISceneManager* smgr = irr_driver->getSceneManager();
	if (!smgr)
	{
		return nullptr;
	}

	irr::scene::SMeshBuffer* buffer = new irr::scene::SMeshBuffer();
	buffer->Material.Lighting = false;
	buffer->Material.BackfaceCulling = false;
	buffer->Material.MaterialType = irr::video::EMT_TRANSPARENT_VERTEX_ALPHA;
	buffer->Material.ZWriteEnable = false;

	const irr::video::SColor lineColor(96, 255, 0, 0);
	const float width = 2.35f;
	const float lift = 0.1f;

	for (size_t i = start; i < end; ++i)
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
		else if (i > 0)
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

	const irr::u32 segment_count = buffer->Vertices.size() / 2;
	if (segment_count < 2)
	{
		buffer->drop();
		return nullptr;
	}

	for (irr::u32 i = 0; i + 1 < segment_count; ++i)
	{
		const irr::u32 idx = i * 2;

		buffer->Indices.push_back(idx);
		buffer->Indices.push_back(idx + 1);
		buffer->Indices.push_back(idx + 2);

		buffer->Indices.push_back(idx + 1);
		buffer->Indices.push_back(idx + 3);
		buffer->Indices.push_back(idx + 2);
	}

	buffer->recalculateBoundingBox();

	irr::scene::SMesh* mesh = new irr::scene::SMesh();
	mesh->addMeshBuffer(buffer);
	mesh->recalculateBoundingBox();

	buffer->drop();

	irr::scene::ISceneNode* node = nullptr;
	bool using_spmesh = false;

#ifndef SERVER_ONLY
	if (CVS->isGLSL())
	{
		SP::SPMesh* spm = SP::convertEVTStandard(mesh, &lineColor);
		if (spm)
		{
            // convertEVTStandard selects a solid material; use alpha blending
            // explicitly so the route's vertex alpha is respected.
            for (irr::u32 i = 0; i < spm->getMeshBufferCount(); ++i)
            {
                spm->getSPMeshBuffer(i)->setSTKMaterial(
                    material_manager->getDefaultSPMaterial("alphablend"));
            }
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

	if (!using_spmesh)
	{
		if (node)
		{
			node->setMaterialFlag(irr::video::EMF_LIGHTING, false);
			node->setMaterialFlag(irr::video::EMF_BACK_FACE_CULLING, false);
			node->setMaterialFlag(irr::video::EMF_ZWRITE_ENABLE, false);
			node->setAutomaticCulling(irr::scene::EAC_OFF);
		}

		mesh->drop();
	}

	if (node)
		node->setVisible(false);

	return node;
}

void Evaluation::clearRouteChunks()
{
	for (irr::scene::ISceneNode* node : m_route_chunks)
	{
		if (node)
		{
			node->remove();
		}
	}
	m_route_chunks.clear();
	m_active_route_chunk = -1;
}

void Evaluation::updateRouteVisibility()
{
	if (m_route_chunks.empty() || m_st_point.empty())
	{
		return;
	}

	const size_t step = ROUTE_CHUNK_POINTS - 1;
	size_t point_index;
	if (m_st_index > 0)
	{
		point_index = static_cast<size_t>(m_st_index);
	}
	else
	{
		point_index = 0;
	}

	point_index = std::min(point_index, m_st_point.size() - 1);

	const size_t current_chunk = std::min(point_index / step, m_route_chunks.size() - 1);

	if (static_cast<int>(current_chunk) == m_active_route_chunk)
	{
		return;
	}

	m_active_route_chunk = static_cast<int>(current_chunk);

	for (size_t i = 0; i < m_route_chunks.size(); ++i)
	{
		if (!m_route_chunks[i])
		{
			continue;
		}
		// 현재 조각과 바로 다음 조각을 표시합니다.
		const bool visible = i == current_chunk || i == current_chunk + 1;

		m_route_chunks[i]->setVisible(visible);
	}
}