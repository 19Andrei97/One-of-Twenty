#pragma once

// CHUNK HASH			///////////////////////////
struct Vector2iHash {
	std::size_t operator()(const sf::Vector2i& v) const noexcept {
		std::size_t h1 = std::hash<int>()(v.x);
		std::size_t h2 = std::hash<int>()(v.y);
		return h1 ^ (h2 << 1);
	}
};

// ELEMENTS ENUM		///////////////////////////
enum class Elements
{
	very_deep_ocean = 0,
	deep_ocean,
	ocean,

	sand,
	hill,
	forest,
	muntain,
	snow,

	clay,
	iron,
	silver,

	test
};

// MAP GENERATOR CLASS	///////////////////////////
class MapGenerator
{

public:
	struct Chunk {
		sf::Vector2i	position;			// top left position of chunk
		sf::VertexArray vertices;			// the map in vertices ready to draw
		std::unordered_map<sf::Vector2i, Elements, Vector2iHash> tile_types;
		bool unload{ true };

		// DEBUG variables
		//std::vector<std::shared_ptr<sf::Text>>	d_noise;
	};

	using ChunkMap = std::unordered_map<sf::Vector2i, std::shared_ptr<Chunk>, Vector2iHash>;

private:
	// CHUNK variables
	ChunkMap	c_chunks;
	int			c_chunk_size;
	int			c_chunk_margin;

	// SHARED variables
	std::atomic<sf::Vector2i>	s_camera_position;
	std::atomic<sf::Vector2i>	s_view_size;
	std::atomic<bool>			s_running{ true };
	
	// THREAD Variables
	BS::thread_pool<>							t_threads{ 3 };
	std::mutex									t_mutex;
	SharedContainer<std::shared_ptr<Chunk>>		tc_chunks_ready;
	SharedContainer<sf::Vector2i>				tc_chunks_in_queue;
	
	// MAP Variables
	int					m_tile_size_px;
	int					m_seed;

	float				m_cont_multiplier;
	float				m_mineral_multiplier;

	double				m_cont_freq;
	double				m_warp_freq;
	double				m_mineral_freq;

	FastNoiseLite		m_noise_continent;
	FastNoiseLite		m_noise_wrap;
	FastNoiseLite		m_noise_mineral;

	std::unordered_map<Elements, sf::Color>		m_biomes;
	std::unordered_map<Elements, float>			m_thresholds;

	// INHERITED variables
	int& i_frames;

	// DEBUG variables
	sf::Font	d_font;
	bool		d_noise_val{ false };
	bool		d_wire_frame{ false };

	// GENERATE MAP SUPPORT FUNCTIONS
	std::shared_ptr<Chunk>		generateChunk(const int height, const int width, const sf::Vector2i& position);
	sf::Color					getBiomeColor(const sf::Vector2i& coord);
	void						startChunksGenerator();

	sf::Vector2i worldToTile(sf::Vector2i pos) const;
	sf::Vector2i tileToWorld(sf::Vector2i tile) const;

public:

	// RESET VARIABLE
	bool m_reset{ false };

	// CONSTRUCTORS
	MapGenerator(sf::Font& font, int& frames,const std::string& map_file)
		: d_font(font)
		, i_frames(frames)
	{
		// Create json
		std::ifstream f(map_file);
		nlohmann::json js_map = nlohmann::json::parse(f);

		// Construct biomes and heights objs
		for (auto& [key, value] : js_map["elements"].items()) {
			m_biomes[static_cast<Elements>(std::stoi(key))] = {
				static_cast<std::uint8_t>(value[0]),
				static_cast<std::uint8_t>(value[1]),
				static_cast<std::uint8_t>(value[2]),
			};
		}

		for (auto& [key, value] : js_map["heights"].items()) {
			m_thresholds[static_cast<Elements>(std::stoi(key))] = value.get<float>();
		}

		// Initiate variables
		m_tile_size_px = js_map["tile_size"];
		m_seed = Random::get(1, 1000000);
		c_chunk_size = js_map["chunk_tile_size"];
		c_chunk_margin = js_map["chunk_margin"];

		m_cont_multiplier = static_cast<float>(js_map["cont_multiplier"]);
		m_mineral_multiplier = static_cast<float>(js_map["mineral_multiplier"]);
		m_cont_freq = static_cast<float>(js_map["cont_freq"]);
		m_warp_freq = static_cast<float>(js_map["warp_freq"]);
		m_mineral_freq = static_cast<float>(js_map["mineral_freq"]);

		// Set Noises
		m_noise_continent.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
		m_noise_continent.SetFractalType(FastNoiseLite::FractalType_FBm);

		m_noise_wrap.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
		m_noise_wrap.SetFractalType(FastNoiseLite::FractalType_FBm);

		m_noise_mineral.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
		m_noise_mineral.SetFractalType(FastNoiseLite::FractalType_FBm);

		setNoises();

		// Generate Thread
		t_threads.submit_task([this] { fillQueueChunks(); }); // Find chunks to create.
		t_threads.submit_task([this] { startChunksGenerator(); });
		t_threads.submit_task([this] { startChunksGenerator(); });
	}

	// DECONSTRUCTOR
	~MapGenerator()
	{
		// Thread cleaning
		s_running = false;
	}

	// RENDERING
	void render(const sf::IntRect& viewBounds, sf::RenderTarget& window);
	void fillQueueChunks();

	// SETTERS
	void setSeed(int seed = Random::get(1, 1000000))	{ m_seed = seed; }
	void setNoises();

	void setContFreq(float freq)						{ m_cont_freq = freq; }
	void setWarpFreq(float freq)						{ m_warp_freq = freq; }
	void setMineralFreq(float freq)						{ m_mineral_freq = freq; }

	void setContMult(float mult)						{ m_cont_multiplier = mult; }
	void setMineralMult(float mult)						{ m_mineral_multiplier = mult; }

	bool setTileColor(const sf::Vector2i& pos, const Elements& new_element);

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
	float						getTileCost(const sf::Vector2i& pos);
	std::vector<std::string>						getPositionInfo(sf::Vector2i pos);
	sf::Vector2i									getLocationWithinBound(sf::Vector2i& pos, float radius);
	std::unordered_map<Elements, sf::Vector2i>		getResourcesWithinBoundary(sf::Vector2i& pos, float radius);

	bool				getDebugNoiseStatus()		const	{ return d_noise_val; }
	bool				getDebugWireFrame()			const	{ return d_wire_frame; }
};

