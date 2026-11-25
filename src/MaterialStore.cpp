/**
 * @file MaterialStore.cpp
 * @brief Implementation of the SQLite material store (see MaterialStore.hpp).
 *
 * Schema versions:
 *  - 0: empty file.
 *  - 1: table `materials`, one row per material, values in SI, CHECK constraints on every value.
 * The version is kept in `PRAGMA user_version`. A file with a newer version is refused, never rewritten.
 */
#include <OpenModel/MaterialStore.hpp>

#include <sqlite3.h>

#include <cmath>
#include <utility>

namespace OpenModel {

namespace {

constexpr int kSchemaVersion = 1;

/** @brief Keys of every class, in enum order, for the CHECK constraint on class_key. */
std::string classKeyList() {
    std::string list;
    for (int i = 0; i <= static_cast<int>(MaterialClass::Stone); ++i) {
        if (!list.empty()) list += ", ";
        list += "'";
        list += materialClassKey(static_cast<MaterialClass>(i));
        list += "'";
    }
    return list;
}

/**
 * @brief DDL of schema version 1. The CHECK constraints mirror validateMaterial() and validateMaterialRecord():
 * a NULL optional value passes the check, as SQL CHECK does, and a present value must be in range.
 */
std::string schemaSql() {
    std::string sql = "CREATE TABLE materials (\n";
    sql += " id TEXT PRIMARY KEY NOT NULL\n";
    sql += "   CHECK (length(id) BETWEEN 1 AND 64 AND id NOT GLOB '*[^a-z0-9_.-]*'),\n";
    sql += " name TEXT NOT NULL CHECK (length(name) BETWEEN 1 AND 200),\n";
    sql += " class_key TEXT NOT NULL CHECK (class_key IN (" + classKeyList() + ")),\n";
    sql += " builtin INTEGER NOT NULL CHECK (builtin IN (0, 1)),\n";
    sql += " youngs_modulus_pa REAL NOT NULL CHECK (youngs_modulus_pa > 0),\n";
    sql += " poisson REAL NOT NULL CHECK (poisson >= 0 AND poisson < 0.5),\n";
    sql += " density_kg_m3 REAL NOT NULL CHECK (density_kg_m3 > 0),\n";
    sql += " note TEXT NOT NULL DEFAULT '',\n";
    sql += " yield_strength_pa REAL CHECK (yield_strength_pa > 0),\n";
    sql += " ultimate_strength_pa REAL CHECK (ultimate_strength_pa > 0),\n";
    sql += " cyclic_strength_coefficient_pa REAL CHECK (cyclic_strength_coefficient_pa > 0),\n";
    sql += " cyclic_strain_hardening_exponent REAL\n";
    sql += "   CHECK (cyclic_strain_hardening_exponent > 0 AND cyclic_strain_hardening_exponent <= 1),\n";
    sql += " fatigue_strength_coefficient_pa REAL CHECK (fatigue_strength_coefficient_pa > 0),\n";
    sql += " fatigue_strength_exponent REAL CHECK (fatigue_strength_exponent < 0),\n";
    sql += " fatigue_ductility_coefficient REAL CHECK (fatigue_ductility_coefficient > 0),\n";
    sql += " fatigue_ductility_exponent REAL CHECK (fatigue_ductility_exponent < 0)\n";
    sql += ")";
    return sql;
}

/** @brief Column list shared by the SELECT and the INSERT/UPDATE statements, in the same order. */
constexpr const char* kColumns =
    "id, name, class_key, builtin, youngs_modulus_pa, poisson, density_kg_m3, note,"
    " yield_strength_pa, ultimate_strength_pa, cyclic_strength_coefficient_pa, cyclic_strain_hardening_exponent,"
    " fatigue_strength_coefficient_pa, fatigue_strength_exponent, fatigue_ductility_coefficient,"
    " fatigue_ductility_exponent";
constexpr int kColumnCount = 16;

/** @brief RAII owner of a prepared statement. */
class Statement {
public:
    Statement() = default;
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    ~Statement() {
        if (stmt_) sqlite3_finalize(stmt_);
    }
    bool prepare(sqlite3* db, const std::string& sql, std::string& error) {
        if (sqlite3_prepare_v2(db, sql.c_str(), static_cast<int>(sql.size()), &stmt_, nullptr) != SQLITE_OK) {
            error = sqlite3_errmsg(db);
            return false;
        }
        return true;
    }
    sqlite3_stmt* get() const noexcept { return stmt_; }

private:
    sqlite3_stmt* stmt_ = nullptr;
};

void bindText(sqlite3_stmt* stmt, int index, const std::string& text) {
    sqlite3_bind_text(stmt, index, text.c_str(), static_cast<int>(text.size()), SQLITE_TRANSIENT);
}

void bindOptional(sqlite3_stmt* stmt, int index, const std::optional<double>& value) {
    if (value) {
        sqlite3_bind_double(stmt, index, *value);
    } else {
        sqlite3_bind_null(stmt, index);
    }
}

std::optional<double> columnOptional(sqlite3_stmt* stmt, int index) {
    if (sqlite3_column_type(stmt, index) == SQLITE_NULL) return std::nullopt;
    return sqlite3_column_double(stmt, index);
}

std::string columnText(sqlite3_stmt* stmt, int index) {
    const auto* text = sqlite3_column_text(stmt, index);
    const int size = sqlite3_column_bytes(stmt, index);
    if (!text || size <= 0) return {};
    return std::string(reinterpret_cast<const char*>(text), static_cast<std::size_t>(size));
}

/** @brief Binds the 16 columns of a record starting at parameter 1 (same order as kColumns). */
void bindRecord(sqlite3_stmt* stmt, const MaterialRecord& record) {
    const ElasticMaterial& e = record.elastic;
    const MaterialFatigueData& f = record.fatigue;
    bindText(stmt, 1, e.id);
    bindText(stmt, 2, e.name);
    bindText(stmt, 3, materialClassKey(e.klass));
    sqlite3_bind_int(stmt, 4, record.builtIn ? 1 : 0);
    sqlite3_bind_double(stmt, 5, e.youngModulus);
    sqlite3_bind_double(stmt, 6, e.poisson);
    sqlite3_bind_double(stmt, 7, e.density);
    bindText(stmt, 8, e.note);
    bindOptional(stmt, 9, f.yieldStrengthPa);
    bindOptional(stmt, 10, f.ultimateStrengthPa);
    bindOptional(stmt, 11, f.cyclicStrengthCoefficientPa);
    bindOptional(stmt, 12, f.cyclicStrainHardeningExponent);
    bindOptional(stmt, 13, f.fatigueStrengthCoefficientPa);
    bindOptional(stmt, 14, f.fatigueStrengthExponent);
    bindOptional(stmt, 15, f.fatigueDuctilityCoefficient);
    bindOptional(stmt, 16, f.fatigueDuctilityExponent);
}

MaterialRecord readRecord(sqlite3_stmt* stmt) {
    MaterialRecord record;
    ElasticMaterial& e = record.elastic;
    e.id = columnText(stmt, 0);
    e.name = columnText(stmt, 1);
    if (!parseMaterialClass(columnText(stmt, 2), e.klass)) e.klass = MaterialClass::OtherMetal;
    record.builtIn = sqlite3_column_int(stmt, 3) != 0;
    e.youngModulus = sqlite3_column_double(stmt, 4);
    e.poisson = sqlite3_column_double(stmt, 5);
    e.density = sqlite3_column_double(stmt, 6);
    e.note = columnText(stmt, 7);
    MaterialFatigueData& f = record.fatigue;
    f.yieldStrengthPa = columnOptional(stmt, 8);
    f.ultimateStrengthPa = columnOptional(stmt, 9);
    f.cyclicStrengthCoefficientPa = columnOptional(stmt, 10);
    f.cyclicStrainHardeningExponent = columnOptional(stmt, 11);
    f.fatigueStrengthCoefficientPa = columnOptional(stmt, 12);
    f.fatigueStrengthExponent = columnOptional(stmt, 13);
    f.fatigueDuctilityCoefficient = columnOptional(stmt, 14);
    f.fatigueDuctilityExponent = columnOptional(stmt, 15);
    return record;
}

/** @brief Rolls back the open transaction; errors are ignored so the caller keeps the original message. */
void rollback(sqlite3* db) {
    char* message = nullptr;
    sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, &message);
    sqlite3_free(message);
}

/** @brief Runs a statement that returns no rows (BEGIN, COMMIT, PRAGMA, DDL). */
bool execute(sqlite3* db, const std::string& sql, std::string& error) {
    char* message = nullptr;
    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &message) != SQLITE_OK) {
        error = message ? message : sqlite3_errmsg(db);
        sqlite3_free(message);
        return false;
    }
    return true;
}

bool finiteAndPositive(double v) {
    return std::isfinite(v) && v > 0.0;
}

} // namespace

bool MaterialFatigueData::any() const noexcept {
    return yieldStrengthPa || ultimateStrengthPa || cyclicStrengthCoefficientPa || cyclicStrainHardeningExponent ||
           fatigueStrengthCoefficientPa || fatigueStrengthExponent || fatigueDuctilityCoefficient ||
           fatigueDuctilityExponent;
}

std::string validateMaterialRecord(const MaterialRecord& record) {
    if (std::string reason = validateMaterial(record.elastic); !reason.empty()) return reason;
    const MaterialFatigueData& f = record.fatigue;
    const auto positive = [](const std::optional<double>& v) { return !v || finiteAndPositive(*v); };
    if (!positive(f.yieldStrengthPa)) return "yield strength must be a positive finite number";
    if (!positive(f.ultimateStrengthPa)) return "ultimate strength must be a positive finite number";
    if (!positive(f.cyclicStrengthCoefficientPa)) return "cyclic strength coefficient K' must be positive";
    if (f.cyclicStrainHardeningExponent &&
        !(finiteAndPositive(*f.cyclicStrainHardeningExponent) && *f.cyclicStrainHardeningExponent <= 1.0)) {
        return "cyclic strain hardening exponent n' must be in (0, 1]";
    }
    if (!positive(f.fatigueStrengthCoefficientPa)) return "fatigue strength coefficient sigma_f' must be positive";
    if (f.fatigueStrengthExponent && !(std::isfinite(*f.fatigueStrengthExponent) && *f.fatigueStrengthExponent < 0.0)) {
        return "fatigue strength exponent b must be negative";
    }
    if (!positive(f.fatigueDuctilityCoefficient)) return "fatigue ductility coefficient epsilon_f' must be positive";
    if (f.fatigueDuctilityExponent && !(std::isfinite(*f.fatigueDuctilityExponent) && *f.fatigueDuctilityExponent < 0.0)) {
        return "fatigue ductility exponent c must be negative";
    }
    if (f.yieldStrengthPa && f.ultimateStrengthPa && *f.yieldStrengthPa > *f.ultimateStrengthPa) {
        return "yield strength cannot exceed the ultimate strength";
    }
    return {};
}

class MaterialStore::Impl {
public:
    sqlite3* db = nullptr;
    std::string path;

    ~Impl() {
        if (db) sqlite3_close(db);
    }

    /** @brief Creates the schema when missing and refuses files written by a newer version. */
    bool migrate(std::string& error) {
        Statement version;
        if (!version.prepare(db, "PRAGMA user_version", error)) return false;
        if (sqlite3_step(version.get()) != SQLITE_ROW) {
            error = sqlite3_errmsg(db);
            return false;
        }
        const int current = sqlite3_column_int(version.get(), 0);
        if (current > kSchemaVersion) {
            error = "the database was written by a newer version (schema " + std::to_string(current) + ")";
            return false;
        }
        if (current == kSchemaVersion) return true;
        if (!execute(db, "BEGIN IMMEDIATE", error)) return false;
        if (!execute(db, schemaSql(), error) ||
            !execute(db, "PRAGMA user_version = " + std::to_string(kSchemaVersion), error) ||
            !execute(db, "COMMIT", error)) {
            rollback(db);
            return false;
        }
        return true;
    }

    /** @brief Writes the built-in entries, updating those whose values changed in the program. */
    bool seedBuiltIns(std::string& error) {
        if (!execute(db, "BEGIN IMMEDIATE", error)) return false;
        const std::string sql = std::string("INSERT INTO materials (") + kColumns + ") VALUES (" +
                                placeholders() + ") ON CONFLICT(id) DO UPDATE SET"
                                " name = excluded.name, class_key = excluded.class_key, builtin = excluded.builtin,"
                                " youngs_modulus_pa = excluded.youngs_modulus_pa, poisson = excluded.poisson,"
                                " density_kg_m3 = excluded.density_kg_m3, note = excluded.note"
                                " WHERE materials.builtin = 1";
        for (const ElasticMaterial& material : materialDatabase()) {
            MaterialRecord record;
            record.elastic = material;
            record.builtIn = true;
            Statement stmt;
            if (!stmt.prepare(db, sql, error)) {
                rollback(db);
                return false;
            }
            bindRecord(stmt.get(), record);
            if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
                error = sqlite3_errmsg(db);
                rollback(db);
                return false;
            }
        }
        return execute(db, "COMMIT", error);
    }

    static std::string placeholders() {
        std::string list;
        for (int i = 1; i <= kColumnCount; ++i) {
            if (i > 1) list += ", ";
            list += "?";
        }
        return list;
    }

    std::optional<MaterialRecord> find(std::string_view id, std::string& error) const {
        Statement stmt;
        if (!stmt.prepare(db, std::string("SELECT ") + kColumns + " FROM materials WHERE id = ?", error)) {
            return std::nullopt;
        }
        bindText(stmt.get(), 1, std::string(id));
        if (sqlite3_step(stmt.get()) != SQLITE_ROW) return std::nullopt;
        return readRecord(stmt.get());
    }
};

MaterialStore::MaterialStore() = default;
MaterialStore::MaterialStore(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
MaterialStore::MaterialStore(MaterialStore&&) noexcept = default;
MaterialStore& MaterialStore::operator=(MaterialStore&&) noexcept = default;
MaterialStore::~MaterialStore() = default;

int MaterialStore::schemaVersion() noexcept {
    return kSchemaVersion;
}

bool MaterialStore::isOpen() const noexcept {
    return impl_ && impl_->db;
}

const std::string& MaterialStore::path() const noexcept {
    static const std::string none;
    return impl_ ? impl_->path : none;
}

bool MaterialStore::open(const std::string& path, MaterialStore& out, std::string& error) {
    if (path.empty()) {
        error = "empty database path";
        return false;
    }
    auto impl = std::make_unique<Impl>();
    impl->path = path;
    if (sqlite3_open_v2(path.c_str(), &impl->db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        error = impl->db ? sqlite3_errmsg(impl->db) : "cannot open the database";
        return false;
    }
    if (!execute(impl->db, "PRAGMA foreign_keys = ON", error)) return false;
    if (!impl->migrate(error) || !impl->seedBuiltIns(error)) return false;
    out = MaterialStore(std::move(impl));
    return true;
}

std::optional<MaterialRecord> MaterialStore::find(std::string_view id) const {
    if (!isOpen()) return std::nullopt;
    std::string error;
    return impl_->find(id, error);
}

std::vector<MaterialRecord> MaterialStore::list() const {
    std::vector<MaterialRecord> records;
    if (!isOpen()) return records;
    std::string error;
    Statement stmt;
    if (!stmt.prepare(impl_->db, std::string("SELECT ") + kColumns + " FROM materials ORDER BY builtin DESC, rowid",
                      error)) {
        return records;
    }
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) records.push_back(readRecord(stmt.get()));
    return records;
}

bool MaterialStore::add(const MaterialRecord& record, std::string& error) {
    if (!isOpen()) {
        error = "the store is not open";
        return false;
    }
    if (record.builtIn) {
        error = "a built-in material cannot be added as custom";
        return false;
    }
    if (std::string reason = validateMaterialRecord(record); !reason.empty()) {
        error = reason;
        return false;
    }
    if (impl_->find(record.elastic.id, error)) {
        error = "the id '" + record.elastic.id + "' is already used";
        return false;
    }
    Statement stmt;
    if (!stmt.prepare(impl_->db, std::string("INSERT INTO materials (") + kColumns + ") VALUES (" +
                                     Impl::placeholders() + ")", error)) {
        return false;
    }
    bindRecord(stmt.get(), record);
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        error = sqlite3_errmsg(impl_->db);
        return false;
    }
    return true;
}

bool MaterialStore::update(std::string_view id, const MaterialRecord& record, std::string& error) {
    if (!isOpen()) {
        error = "the store is not open";
        return false;
    }
    const auto current = impl_->find(id, error);
    if (!current) {
        if (error.empty()) error = "no material with id '" + std::string(id) + "'";
        return false;
    }
    if (current->builtIn) {
        error = "built-in materials cannot be changed";
        return false;
    }
    if (record.elastic.id != id) {
        error = "the id of a material cannot change";
        return false;
    }
    if (std::string reason = validateMaterialRecord(record); !reason.empty()) {
        error = reason;
        return false;
    }
    Statement stmt;
    if (!stmt.prepare(impl_->db,
                      std::string("UPDATE materials SET name = ?2, class_key = ?3, builtin = ?4,"
                                  " youngs_modulus_pa = ?5, poisson = ?6, density_kg_m3 = ?7, note = ?8,"
                                  " yield_strength_pa = ?9, ultimate_strength_pa = ?10,"
                                  " cyclic_strength_coefficient_pa = ?11, cyclic_strain_hardening_exponent = ?12,"
                                  " fatigue_strength_coefficient_pa = ?13, fatigue_strength_exponent = ?14,"
                                  " fatigue_ductility_coefficient = ?15, fatigue_ductility_exponent = ?16"
                                  " WHERE id = ?1 AND builtin = 0"),
                      error)) {
        return false;
    }
    bindRecord(stmt.get(), record);
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        error = sqlite3_errmsg(impl_->db);
        return false;
    }
    if (sqlite3_changes(impl_->db) != 1) {
        error = "the material was not updated";
        return false;
    }
    return true;
}

bool MaterialStore::remove(std::string_view id, std::string& error) {
    if (!isOpen()) {
        error = "the store is not open";
        return false;
    }
    const auto current = impl_->find(id, error);
    if (!current) {
        if (error.empty()) error = "no material with id '" + std::string(id) + "'";
        return false;
    }
    if (current->builtIn) {
        error = "built-in materials cannot be removed";
        return false;
    }
    Statement stmt;
    if (!stmt.prepare(impl_->db, "DELETE FROM materials WHERE id = ? AND builtin = 0", error)) return false;
    bindText(stmt.get(), 1, std::string(id));
    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        error = sqlite3_errmsg(impl_->db);
        return false;
    }
    return sqlite3_changes(impl_->db) == 1;
}

bool MaterialStore::importTsv(const std::string& path, std::vector<std::string>& problems, std::string& error) {
    if (!isOpen()) {
        error = "the store is not open";
        return false;
    }
    MaterialLibrary library;
    if (!library.load(path, problems, error)) return false;
    for (const ElasticMaterial& entry : library.entries()) {
        if (library.isBuiltIn(entry.id)) continue;
        MaterialRecord record;
        record.elastic = entry;
        std::string reason;
        if (!add(record, reason)) problems.push_back("'" + entry.id + "': " + reason);
    }
    return true;
}

} // namespace OpenModel
