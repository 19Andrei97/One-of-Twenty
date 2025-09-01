#pragma once

#include <sstream>
#include <fstream>
#include <iostream>

#include <memory>
#include <cmath>
#include <future>
#include <vector>
#include <unordered_map>

#include <SFML/Graphics.hpp>
#include <SFML/OpenGL.hpp>

#include "FastNoiseLite.h"
#include "Random.h"
#include "ThreadSafeQueue.h"
#include "json.h"

// CHUNK HASH			///////////////////////////
struct Vector2iHash {
	std::size_t operator()(const sf::Vector2i& v) const noexcept {
		std::size_t h1 = std::hash<int>()(v.x);
		std::size_t h2 = std::hash<int>()(v.y);
		return h1 ^ (h2 << 1);
	}
};

// MAP GENERATOR CLASS	///////////////////////////
class MapGenerator
{

public:
	struct Chunk {
		sf::Vector2i	position;			// top left position of chunk
		sf::VertexArray vertices;			// the map in vertices ready to draw
		int				id;					// unique id

		// DEBUG variables
		std::vector<std::shared_ptr<sf::Text>>	d_noise;

		Chunk() { id = m_chunk_ids++; }
	};

private:
	// CHUNK variables
	static int																	m_chunk_ids;			// Variable for chunk ids
	std::unordered_map<sf::Vector2i, std::shared_ptr<Chunk>, Vector2iHash>		m_chunks;
	std::atomic<sf::Vector2i>													m_cameraPos;
	std::atomic<sf::Vector2i>													m_viewSize;
	ThreadSafeQueue<std::shared_ptr<Chunk>>										m_readyChunks;
	std::mutex																	m_chunksMutex;
	const int																	maxChunksPerCycle{ 8 };	// maximum chunk generators
	const int																	m_chunkSize{ 32 };		// chunk tile size
	const int																	m_unloadMargin{ 5 };	// chunks beyond view before unloading 
	
	// THREAD Variables
	std::thread						m_workerThread;
	std::atomic<bool>				m_running{ true };
	ThreadSafeQueue<sf::Vector2i>	m_requestQueue;
	
	// MAP Variables
	int					m_tile_size_px{ 0 };
	int					m_seed{ 0 };

	float				m_cont_multiplier{ 0.018f };
	float				m_mineral_multiplier{ 0.018f };

	double				m_cont_freq{ 0.023 };
	double				m_warp_freq{ 0.007 };
	double				m_mineral_freq{ 0.023 };

	FastNoiseLite		m_continentNoise;
	FastNoiseLite		m_warpNoise;
	FastNoiseLite		m_mineralNoise;

	std::unordered_map<std::string, sf::Color>		m_biomes;
	std::unordered_map<std::string, float>			m_thresholds;

	// DEBUG variables
	sf::Font	d_font;
	bool		d_noise_val{ false };
	bool		d_wire_frame{ false };

	// INHERITED variables
	int&		i_frames;

	// GENERATE MAP SUPPORT FUNCTIONS
	std::shared_ptr<Chunk>&		generateChunk(const int height, const int width, const sf::Vector2i& position);
	sf::Color					getBiomeColor(float height, float temp);
	void						startWorker();

public:

	// RESET VARIABLE
	bool m_reset{ false };

	// CONSTRUCTORS
	MapGenerator(sf::Font& font, int& frames, const std::string& biomes_file)
		: d_font(font)
		, i_frames(frames)
	{

		// Set Noises
		m_continentNoise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
		m_continentNoise.SetFractalType(FastNoiseLite::FractalType_FBm);

		m_warpNoise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
		m_warpNoise.SetFractalType(FastNoiseLite::FractalType_FBm);

		m_mineralNoise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
		m_mineralNoise.SetFractalType(FastNoiseLite::FractalType_FBm);

		setNoises();

		// Generate Thread
		m_workerThread = std::thread(&MapGenerator::startWorker, this);

		// Construct biomes and heights objs
		std::ifstream f(biomes_file);
		nlohmann::json j = nlohmann::json::parse(f);

		for (auto& [key, value] : j["elements"].items()) {
			m_biomes[key] = {
				static_cast<std::uint8_t>(value[0]),
				static_cast<std::uint8_t>(value[1]),
				static_cast<std::uint8_t>(value[2]),
			};
		}

		for (auto& [key, value] : j["heights"].items()) {
			m_thresholds[key] = value.get<float>();
		}
	}
	
	~MapGenerator()
	{
		// Thread cleaning
		m_running = false;
		m_requestQueue.shutdown();
		if (m_workerThread.joinable())
			m_workerThread.join();
	}

	// RENDERING
	void render(const sf::IntRect& viewBounds, sf::RenderTarget& window);

	// SETTERS/SET MAP PARAMETERS
	void setMap	(int tile_size_px, int seed = Random::get(1, 1000000));
	void setSeed(int seed = Random::get(1, 1000000))	{ m_seed = seed; }
	void setNoises();

	void setContFreq(float freq)						{ m_cont_freq = freq; }
	void setWarpFreq(float freq)						{ m_warp_freq = freq; }
	void setMineralFreq(float freq)						{ m_mineral_freq = freq; }

	void setContMult(float mult)						{ m_cont_multiplier = mult; }
	void setMineralMult(float mult)						{ m_mineral_multiplier = mult; }

	// DEBUG
	void setDebugNoiseView(bool status)					{ d_noise_val = status; }
	void setDebugWireFrame(bool status)					{ d_wire_frame = status; }
	void print()
	{
		std::cout << "Seed: " << m_seed << "\n";
		std::cout << "Mineral Frequency: " << m_mineral_freq << "\n";
		std::cout << "Continent Frequency: " << m_cont_freq << "\n";
		std::cout << "Warp Frequency: " << m_warp_freq << "\n\n";
		std::cout << "Mineral Multiplier: " << m_mineral_multiplier << "\n";
		std::cout << "Continent Multiplier: " << m_cont_multiplier << "\n";
		std::cout << "------------------------\n\n";
	}

	// GETTERS
	int					getTileSize()				const	{ return m_tile_size_px; }
	int					getSeed()					const	{ return m_seed; }
	bool				getDebugNoiseStatus()		const	{ return d_noise_val; }
	bool				getDebugWireFrame()			const	{ return d_wire_frame; }
};

