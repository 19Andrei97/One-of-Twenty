#pragma once

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

		// DEBUG variables
		std::vector<std::shared_ptr<sf::Text>>	d_noise;
	};

private:
	// CHUNK variables
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
	float				m_mineral_multiplier{ 0.15f };

	double				m_cont_freq{ 0.023 };
	double				m_warp_freq{ 0.007 };
	double				m_mineral_freq{ 0.004 };


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

	sf::Vector2i worldToTile(sf::Vector2f pos) const;
	sf::Vector2f tileToWorld(sf::Vector2i tile) const;

public:

	// RESET VARIABLE
	bool m_reset{ false };

	// CONSTRUCTORS
	MapGenerator(sf::Font& font, int& frames, const std::string& biomes_file)
		: d_font(font)
		, i_frames(frames)
	{
		// Set Noises
		m_continentNoise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
		m_continentNoise.SetFractalType(FastNoiseLite::FractalType_FBm);

		m_warpNoise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
		m_warpNoise.SetFractalType(FastNoiseLite::FractalType_FBm);

		m_mineralNoise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
		m_mineralNoise.SetFractalType(FastNoiseLite::FractalType_FBm);

		setNoises();

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

		// Initiate variables
		m_tile_size_px = j["tile_size"];
		m_seed = Random::get(1, 1000000);

		// Generate Thread
		m_workerThread = std::thread(&MapGenerator::startWorker, this);
	}

	// DECONSTRUCTOR
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

	// SETTERS
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
		LOG_INFO("Seed: {}.", m_seed);
		LOG_INFO("Mineral Frequency: {}.", m_mineral_freq);
		LOG_INFO("Continent Frequency: {}.", m_cont_freq);
		LOG_INFO("Warp Frequency: {}.", m_warp_freq);
		LOG_INFO("Mineral Multiplier: {}.", m_mineral_multiplier);
		LOG_INFO("Continent Multiplier: {}.", m_cont_multiplier);
	}

	// GETTERS
	int							getTileSize()				const	{ return m_tile_size_px; }
	int							getSeed()					const	{ return m_seed; }
	std::vector<std::string>	getPositionInfo(sf::Vector2f pos);
	sf::Vector2f				getLocationWithinBound(sf::Vector2f& pos, float radius);

	bool				getDebugNoiseStatus()		const	{ return d_noise_val; }
	bool				getDebugWireFrame()			const	{ return d_wire_frame; }
};

