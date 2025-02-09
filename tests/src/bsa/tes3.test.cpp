#include "utility.hpp"

#include <array>
#include <cstddef>
#include <cstring>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "catch2.hpp"
#include <mmio/mmio.hpp>

#include "bsa/tes3.hpp"

static_assert(assert_nothrowable<bsa::tes3::hashing::hash>());
static_assert(assert_nothrowable<bsa::tes3::file>());
static_assert(assert_nothrowable<bsa::tes3::file::key, false>());
static_assert(assert_nothrowable<bsa::tes3::archive>());

TEST_CASE("bsa::tes3::hashing", "[src][tes3][hashing]")
{
	SECTION("hashes start empty")
	{
		const bsa::tes3::hashing::hash h;
		REQUIRE(h.lo == 0);
		REQUIRE(h.hi == 0);
		REQUIRE(h.numeric() == 0);
	}

	SECTION("validate hash values")
	{
		const auto h = [](std::string_view a_path) noexcept {
			return bsa::tes3::hashing::hash_file(a_path).numeric();
		};

		REQUIRE(h("meshes/c/artifact_bloodring_01.nif"sv) == 0x1C3C1149920D5F0C);
		REQUIRE(h("meshes/x/ex_stronghold_pylon00.nif"sv) == 0x20250749ACCCD202);
		REQUIRE(h("meshes/r/xsteam_centurions.kf"sv) == 0x6E5C0F3125072EA6);
		REQUIRE(h("textures/tx_rock_cave_mu_01.dds"sv) == 0x58060C2FA3D8F759);
		REQUIRE(h("meshes/f/furn_ashl_chime_02.nif"sv) == 0x7C3B2F3ABFFC8611);
		REQUIRE(h("textures/tx_rope_woven.dds"sv) == 0x5865632F0C052C64);
		REQUIRE(h("icons/a/tx_templar_skirt.dds"sv) == 0x46512A0B60EDA673);
		REQUIRE(h("icons/m/misc_prongs00.dds"sv) == 0x51715677BBA837D3);
		REQUIRE(h("meshes/i/in_c_stair_plain_tall_02.nif"sv) == 0x2A324956BF89B1C9);
		REQUIRE(h("meshes/r/xkwama worker.nif"sv) == 0x6D446E352C3F5A1E);
	}

	SECTION("forward slashes '/' are treated the same as backwards slashes '\\'")
	{
		REQUIRE(bsa::tes3::hashing::hash_file("foo/bar/baz") ==
				bsa::tes3::hashing::hash_file("foo\\bar\\baz"));
	}

	SECTION("hashes are case-insensitive")
	{
		REQUIRE(bsa::tes3::hashing::hash_file("FOO/BAR/BAZ") ==
				bsa::tes3::hashing::hash_file("foo/bar/baz"));
	}

	SECTION("hashes are sorted first by their low value, then their high value")
	{
		const bsa::tes3::hashing::hash lhs{ 0, 1 };
		const bsa::tes3::hashing::hash rhs{ 1, 0 };
		REQUIRE(lhs < rhs);
	}
}

TEST_CASE("bsa::tes3::file", "[src][tes3][vfs]")
{
	SECTION("files start empty")
	{
		const bsa::tes3::file f;
		REQUIRE(f.empty());
		REQUIRE(f.size() == 0);
		REQUIRE(f.as_bytes().empty());
	}
}

TEST_CASE("bsa::tes3::archive", "[src][tes3][archive]")
{
	SECTION("archives start empty")
	{
		const bsa::tes3::archive bsa;
		REQUIRE(bsa.empty());
		REQUIRE(bsa.begin() == bsa.end());
		REQUIRE(bsa.size() == 0);
	}

	SECTION("we can read archives")
	{
		const std::filesystem::path root{ "tes3_read_test"sv };

		bsa::tes3::archive bsa;
		bsa.read(root / "test.bsa"sv);
		REQUIRE(!bsa.empty());

		constexpr std::array files{
			"characters/character_0000.png"sv,
			"share/License.txt"sv,
		};

		for (const auto& name : files) {
			const auto p = root / name;
			REQUIRE(std::filesystem::exists(p));

			const auto archived = bsa[name];
			REQUIRE(archived);
			REQUIRE(archived->size() == std::filesystem::file_size(p));

			const auto disk = map_file(p);

			assert_byte_equality(archived->as_bytes(), std::span{ disk.data(), disk.size() });
		}
	}

	SECTION("we can write archives")
	{
		const std::filesystem::path root{ "tes3_write_test"sv };

		struct info_t
		{
			consteval info_t(
				std::uint32_t a_lo,
				std::uint32_t a_hi,
				std::string_view a_path) noexcept :
				hash{ a_lo, a_hi },
				path(a_path)
			{}

			bsa::tes3::hashing::hash hash;
			std::string_view path;
		};

		constexpr std::array index{
			info_t{ 0x0C18356B, 0xA578DB74, "Tiles/tile_0001.png"sv },
			info_t{ 0x1B0D3416, 0xF5D5F30E, "Share/License.txt"sv },
			info_t{ 0x1B3B140A, 0x07B36E53, "Background/background_middle.png"sv },
			info_t{ 0x29505413, 0x1EB4CED7, "Construct 3/Pixel Platformer.c3p"sv },
			info_t{ 0x4B7D031B, 0xD4701AD4, "Tilemap/characters_packed.png"sv },
			info_t{ 0x74491918, 0x2BEBCD0A, "Characters/character_0001.png"sv },
		};

		std::vector<mmio::mapped_file_source> mmapped;
		bsa::tes3::archive in;
		for (const auto& file : index) {
			const auto& data = mmapped.emplace_back(
				map_file(root / "data"sv / file.path));
			REQUIRE(data.is_open());
			bsa::tes3::file f;
			f.set_data({ //
				reinterpret_cast<const std::byte*>(data.data()),
				data.size() });

			REQUIRE(in.insert(file.path, std::move(f)).second);
		}

		binary_io::any_ostream os{ std::in_place_type<binary_io::memory_ostream> };
		in.write(os);

		bsa::tes3::archive out;
		out.read({ os.get<binary_io::memory_ostream>().rdbuf() });
		REQUIRE(out.size() == index.size());
		for (std::size_t idx = 0; idx < index.size(); ++idx) {
			const auto& file = index[idx];
			const auto& mapped = mmapped[idx];

			REQUIRE(out[file.path]);

			const auto f = out.find(file.path);
			REQUIRE(f != out.end());
			REQUIRE(f->first.hash() == file.hash);
			REQUIRE(f->first.name() == simple_normalize(file.path));
			assert_byte_equality(f->second.as_bytes(), std::span{ mapped.data(), mapped.size() });
		}
	}

	SECTION("archives will bail on malformed inputs")
	{
		const std::filesystem::path root{ "tes3_invalid_test"sv };
		constexpr std::array types{
			"magic"sv,
			"exhausted"sv,
		};

		for (const auto& type : types) {
			std::string filename;
			filename += "invalid_"sv;
			filename += type;
			filename += ".bsa"sv;

			bsa::tes3::archive bsa;
			REQUIRE_THROWS_WITH(
				bsa.read(root / filename),
				make_substr_matcher(type));
		}
	}

	SECTION("we can validate the offsets within an archive (<4gb)")
	{
		bsa::tes3::archive bsa;
		const auto add =
			[&](bsa::tes3::hashing::hash a_hash,
				std::span<const std::byte> a_data) {
				bsa::tes3::file f;
				f.set_data(a_data);
				REQUIRE(bsa.insert(a_hash, std::move(f)).second);
			};

		constexpr auto largesz = static_cast<std::uint64_t>((std::numeric_limits<std::uint32_t>::max)()) + 1u;
		const auto plarge = std::make_unique<std::byte[]>(largesz);
		const std::span large{ plarge.get(), largesz };

		const std::array<std::byte, 1u << 4> littlebuf{};
		const std::span little{ littlebuf.data(), littlebuf.size() };

		const auto verify = [&]() {
			return bsa.verify_offsets();
		};

		REQUIRE(verify());

		add({ 0 }, little);
		REQUIRE(verify());

		add({ 1 }, large);
		REQUIRE(verify());

		bsa.clear();
		add({ 0 }, large);
		REQUIRE(verify());

		add({ 1 }, little);
		REQUIRE(!verify());
	}

	SECTION("we can read/write archives without touching the disk")
	{
		test_in_memory_buffer<bsa::tes3::archive>(
			"tes3.bsa"sv,
			[](
				bsa::tes3::archive& a_archive,
				std::span<const std::pair<std::string_view, mmio::mapped_file_source>> a_files) {
				for (const auto& [path, file] : a_files) {
					bsa::tes3::file f;
					f.set_data(std::span{ file.data(), file.size() });
					a_archive.insert(path, std::move(f));
				}
			},
			[](bsa::tes3::archive& a_archive, std::filesystem::path a_dst) {
				a_archive.write(a_dst);
			},
			[](bsa::tes3::archive& a_archive, binary_io::any_ostream& a_dst) {
				a_archive.write(a_dst);
			},
			[](bsa::tes3::archive& a_archive, std::span<const std::byte> a_src, bsa::copy_type a_type) {
				a_archive.read({ a_src, a_type });
			});
	}
}
