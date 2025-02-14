#include "LCSToolsImpl.h"
#include <QCoreApplication>
#include <QDebug>
#include <QJsonDocument>
#include <QMetaEnum>
#include <QThread>
#include <QStandardPaths>
#include <fstream>

static QString to_qstring(LCS::fs::path const& path) {
    return QString::fromStdU16String(path.generic_u16string());
}

static QString to_qstring(std::u8string const& str) {
    return QString::fromUtf8((char const*)str.data(), (int)str.size());
}

static QString to_qstring(const char* str) {
    return QString(str);
}

static QString to_qstring(std::string const& str) {
    return QString::fromStdString(str);
}

static QString to_qstring(std::runtime_error const& error) {
    if (auto const err = dynamic_cast<LCS::fs::filesystem_error const*>(&error)) {
        return to_qstring(LCS::to_u8string(*err));
    }
    return to_qstring(error.what());
}

LCSToolsImpl::LCSToolsImpl(QObject *parent)
    : QObject(parent), progDirPath_(QCoreApplication::applicationDirPath().toStdU16String()),
      patcherConfig_(progDirPath_ / "lolcustomskin.txt") {}

LCSToolsImpl::~LCSToolsImpl() {
    if (lockfile_) {
        delete lockfile_;
    }
}

LCSToolsImpl::LCSState LCSToolsImpl::getState() {
    return state_;
}

void LCSToolsImpl::setState(LCSState value) {
    if (state_ != value) {
        state_ = value;
        emit stateChanged(value);
    }
}

void LCSToolsImpl::setStatus(QString status) {
    if (status_ != status) {
        status_ = status;
        emit statusChanged(status);
    }
}

QString LCSToolsImpl::getLeaguePath() {
    return to_qstring(leaguePath_);
}

/// util
QJsonArray LCSToolsImpl::listProfiles() {
    QJsonArray profiles;
    auto profilesPath = progDirPath_ / "profiles";
    if (!LCS::fs::exists(profilesPath)) {
        LCS::fs::create_directories(profilesPath);
    }
    for(auto const& entry: LCS::fs::directory_iterator(profilesPath)) {
        auto path = entry.path();
        if (entry.is_directory() && !LCS::fs::exists(path.generic_u8string() + u8".profile")) {
            std::error_code error = {};
            LCS::fs::remove_all(path, error);
        } else if (entry.is_regular_file() && path.extension() == ".profile") {
            auto name = path.filename();
            name.replace_extension();
            profiles.push_back(to_qstring(name));
        }
    }
    if (!profiles.contains("Default Profile")) {
        profiles.push_front("Default Profile");
    }
    return profiles;
}

QJsonObject LCSToolsImpl::readProfile(QString profileName) {
    QJsonObject profile;
    LCS::fs::path profile_name = progDirPath_ / "profiles" / (profileName + ".profile").toStdU16String();
    std::ifstream infile(profile_name, std::ios::binary);
    std::string line;
    while(std::getline(infile, line)) {
        if (line.empty()) {
            continue;
        }
        profile.insert(QString::fromUtf8(line.data(), (int)line.size()), true);
    }
    return profile;
}

void LCSToolsImpl::writeProfile(QString profileName, QJsonObject profile) {
    LCS::fs::path profile_name = progDirPath_ / "profiles" / (profileName + ".profile").toStdU16String();
    std::ofstream outfile(profile_name, std::ios::binary);
    for(QString const& mod: profile.keys()) {
        auto modname = mod.toUtf8();
        if (modname.size() == 0) {
            continue;
        }
        outfile.write(modname.data(), modname.size());
        outfile << '\n';
    }
}

QString LCSToolsImpl::readCurrentProfile() {
    QJsonObject profile;
    std::ifstream infile(progDirPath_ / "current.profile", std::ios::binary);
    std::string line;
    if (!std::getline(infile, line) || line.empty()) {
        line = "Default Profile";
    }
    return QString::fromUtf8(line.data(), (int)line.size());
}

void LCSToolsImpl::writeCurrentProfile(QString profile) {
    std::ofstream outfile(progDirPath_ / "current.profile", std::ios::binary);
    auto profiledata = profile.toUtf8();
    outfile.write(profiledata.data(), profiledata.size());
    outfile << '\n';
}

LCS::WadIndex const& LCSToolsImpl::wadIndex() {
    if (leaguePath_.empty()) {
        lcs_hint(u8"Select your Game directory!");
        throw std::runtime_error("Game path not set!");
    }

    while (wadIndex_ == nullptr || !wadIndex_->is_uptodate()) {
        lcs_hint(u8"Is this Game path correct: ", leaguePath_.generic_u8string(), u8" ?\n",
                 "Try repairing or reinstalling the Game!");
        wadIndex_ = std::make_unique<LCS::WadIndex>(leaguePath_, blacklist_, ignorebad_);
        QThread::msleep(250);
    }

    return *wadIndex_;
}

namespace {
    QJsonObject validateAndCorrect(QString fileName, QJsonObject object) {
        if (!object.contains("Name") || !object["Name"].isString() || object["Name"].toString().isEmpty()) {
            object["Name"] = fileName;
        }
        if (!object.contains("Version") || !object["Version"].isString()) {
            object["Version"] = "0.0.0";
        }
        if (!object.contains("Author") || !object["Author"].isString()) {
            object["Author"] = "UNKNOWN";
        }
        if (!object.contains("Description") || !object["Description"].isString()) {
            object["Description"] = "";
        }
        return object;
    }

    QJsonObject parseInfoData(QString fileName, std::u8string const& str) {
        QByteArray data = QByteArray((char const*)str.data(), (int)str.size());
        QJsonParseError error;
        auto document = QJsonDocument::fromJson(data, &error);
        if (!document.isObject()) {
            return validateAndCorrect(fileName, QJsonObject());
        }
        return validateAndCorrect(fileName, document.object());
    }

    QJsonObject parseInfoData(LCS::fs::path const& fileName, std::u8string const& str) {
        return parseInfoData(to_qstring(fileName), str);
    }

    std::u8string dumpInfoData(QJsonObject info) {
        QJsonDocument document(info);
        auto data = document.toJson();
        return { data.begin(), data.end() };
    }
}

/// impl

void LCSToolsImpl::changeLeaguePath(QString newLeaguePath) {
    if (state_ == LCSState::StateIdle || state_ == LCSState::StateUnitialized) {
        if (state_ != LCSState::StateUnitialized) {
            setState(LCSState::StateBusy);
            setStatus("Change League Path");
        }
        LCS::fs::path path = newLeaguePath.toStdU16String();
        if (!LCS::fs::exists(path / "League of Legends.exe") && !LCS::fs::exists(path / "League of Legends.app")) {
            path = "";
        }
        if (leaguePath_ != path) {
            LCS::path_remap()[u8"<Game>"] = path.generic_u8string();
            leaguePath_ = path;
            wadIndex_ = nullptr;
            emit leaguePathChanged(to_qstring(leaguePath_));
        }
        if (state_ != LCSState::StateUnitialized) {
            setState(LCSState::StateIdle);
        }
    }
}

void LCSToolsImpl::changeBlacklist(bool blacklist) {
    if (state_ == LCSState::StateIdle || state_ == LCSState::StateUnitialized) {
        if (blacklist_ != blacklist) {
            if (state_ != LCSState::StateUnitialized) {
                setState(LCSState::StateBusy);
                setStatus("Toggle blacklist");
            }
            blacklist_ = blacklist;
            wadIndex_ = nullptr;
            emit blacklistChanged(blacklist);
            if (state_ != LCSState::StateUnitialized) {
                setState(LCSState::StateIdle);
            }
        }
    }
}

void LCSToolsImpl::changeIgnorebad(bool ignorebad) {
    if (state_ == LCSState::StateIdle || state_ == LCSState::StateUnitialized) {
        if (ignorebad_ != ignorebad) {
            if (state_ != LCSState::StateUnitialized) {
                setState(LCSState::StateBusy);
                setStatus("Toggle ignorebad");
            }
            ignorebad_ = ignorebad;
            wadIndex_ = nullptr;
            emit ignorebadChanged(ignorebad);
            if (state_ != LCSState::StateUnitialized) {
                setState(LCSState::StateIdle);
            }
        }
    }
}


void LCSToolsImpl::init() {
    if (state_ == LCSState::StateUnitialized) {
        setState(LCSState::StateBusy);
        setStatus("Acquire lock");
        constexpr auto remap_location = [] (std::u8string name, QStandardPaths::StandardLocation type) {
            LCS::fs::path path = QStandardPaths::writableLocation(type).toStdU16String();
            LCS::path_remap()[name] = path.generic_u8string();
        };
        LCS::path_remap()[u8"<LCS>"] = progDirPath_.generic_u8string();
        LCS::path_remap()[u8"<Game>"] = leaguePath_.generic_u8string();
        LCS::path_remap()[u8"<PWD>"] = LCS::fs::current_path().generic_u8string();
        remap_location(u8"<Desktop>", QStandardPaths::DesktopLocation);
        remap_location(u8"<Documents>", QStandardPaths::DocumentsLocation);
        remap_location(u8"<Downloads>", QStandardPaths::DownloadLocation);
        remap_location(u8"<Home>", QStandardPaths::HomeLocation);

        auto lockpath = to_qstring((progDirPath_/ "lockfile"));
        lockfile_ = new QLockFile(lockpath);
        try {
            lcs_hint(u8"Make sure you already don't have LCS running!");
            if (!lockfile_->tryLock()) {
                auto lockerror = QString::number((int)lockfile_->error());
                throw std::runtime_error("Lock error: " + lockerror.toStdString());
            }
        } catch (std::runtime_error const& error) {
            emit_reportError("Check already running", error);
            setState(LCSState::StateCriticalError);
            return;
        }

        setStatus("Load mods");
        try {
            patcher_.load(patcherConfig_);
            modIndex_ = std::make_unique<LCS::ModIndex>(progDirPath_ / "installed");
            QJsonObject mods;
            for(auto const& [rawFileName, rawMod]: modIndex_->mods()) {
                mods.insert(to_qstring(rawFileName),
                            parseInfoData(rawFileName, rawMod->info()));
            }
            auto profiles = listProfiles();
            auto profileName = readCurrentProfile();
            if (!profiles.contains(profileName)) {
                profileName = "Default Profile";
                writeCurrentProfile(profileName);
            }
            auto profileMods = readProfile(profileName);
            emit initialized(mods, profiles, profileName, profileMods);
            setState(LCSState::StateIdle);
        } catch(std::runtime_error const& error) {
            emit_reportError("Load mods", error);
            setState(LCSState::StateCriticalError);
        }
    }
}

void LCSToolsImpl::deleteMod(QString name) {
    if (state_ == LCSState::StateIdle) {
        setState(LCSState::StateBusy);
        setStatus("Delete mod");
        try {
            modIndex_->remove(name.toStdU16String());
            emit modDeleted(name);
        } catch(std::runtime_error const& error) {
            emit_reportError("", error);
        }
        setState(LCSState::StateIdle);
    }
}

void LCSToolsImpl::exportMod(QString name, QString dest) {
    if (state_ == LCSState::StateIdle) {
        setState(LCSState::StateBusy);
        setStatus("Export mod");
        try {
            auto const mod = modIndex_->get_mod(name.toStdU16String());
            mod->write_zip(dest.toStdU16String(), *dynamic_cast<LCS::ProgressMulti*>(this));
        } catch(std::runtime_error const& error) {
            emit_reportError("Export mod", error);
        }
        setState(LCSState::StateIdle);
    }
}

void LCSToolsImpl::installFantomeZip(QStringList paths) {
    if (state_ == LCSState::StateIdle) {
        setState(LCSState::StateBusy);
        setStatus("Installing Mod");
        try {
            auto const& index = wadIndex();
            for (QString path: paths) {
                path = path.replace('\\', '/');
                auto& progress = *dynamic_cast<LCS::ProgressMulti*>(this);
                LCS::Mod* mod = modIndex_->install(path.toStdU16String(), index, progress);
                emit installedMod(to_qstring(mod->filename()),
                                  parseInfoData(mod->filename(), mod->info()));
            }
        } catch(std::runtime_error const& error) {
            emit_reportError("Installing a mod", error);
        }
        setState(LCSState::StateIdle);
    }
}

void LCSToolsImpl::makeMod(QString fileName, QJsonObject infoData, QString image) {
    if (state_ == LCSState::StateIdle) {
        setState(LCSState::StateBusy);
        setStatus("Make mod");
        try {
            infoData = validateAndCorrect(fileName, infoData);
            auto mod = modIndex_->make(fileName.toStdU16String(),
                                       dumpInfoData(infoData),
                                       image.toStdU16String());
            emit modCreated(to_qstring(mod->filename()),
                            infoData,
                            to_qstring(mod->image()));
        } catch(std::runtime_error const& error) {
            emit_reportError("Make mod", error);
        }
        setState(LCSState::StateIdle);
    }
}

void LCSToolsImpl::saveProfile(QString name, QJsonObject mods, bool run, bool skipConflict) {
    LCS::Conflict conflictStrategy = skipConflict ? LCS::Conflict::Skip : LCS::Conflict::Abort;
    if (state_ == LCSState::StateIdle) {
        setState(LCSState::StateBusy);
        setStatus("Save profile");
        if (name.isEmpty() || name.isNull()) {
            name = "Default Profile";
        }
        try {
            auto const& index = wadIndex();
            LCS::WadMergeQueue queue(progDirPath_ / "profiles" / name.toStdU16String(), index);
            for(QString const& key: mods.keys()) {
                LCS::fs::path fileName = key.toStdU16String();
                if (auto i = modIndex_->mods().find(fileName); i != modIndex_->mods().end()) {
                    queue.addMod(i->second.get(), conflictStrategy);
                }
            }
            queue.write(*dynamic_cast<LCS::ProgressMulti*>(this));
            queue.cleanup();
            writeCurrentProfile(name);
            writeProfile(name, mods);
            emit profileSaved(name, mods);
        } catch(std::runtime_error const& error) {
            emit_reportError("Save profile", error);
        }
        setState(LCSState::StateIdle);
    }
    if (run) {
        runProfile(name);
    }
}

void LCSToolsImpl::loadProfile(QString name) {
    if (state_ == LCSState::StateIdle) {
        setState(LCSState::StateBusy);
        setStatus("Save profile");
        if (name.isEmpty() || name.isNull()) {
            name = "Default Profile";
        }
        try {
            auto profileMods = readProfile(name);
            emit profileLoaded(name, profileMods);
        } catch(std::runtime_error const& error) {
            emit_reportError("Load profile", error);
        }
        setState(LCSState::StateIdle);
    }
}

void LCSToolsImpl::deleteProfile(QString name) {
    if (state_ == LCSState::StateIdle) {
        setState(LCSState::StateBusy);
        setStatus("Delete profile");
        try {
            LCS::fs::remove(progDirPath_ / "profiles" / (name + ".profile").toStdU16String());
            LCS::fs::remove_all(progDirPath_ / "profiles" / name.toStdU16String());
            emit profileDeleted(name);
        } catch(std::runtime_error const& error) {
            emit_reportError("Delete profile", error);
        }
        setState(LCSState::StateIdle);
    }
}

void LCSToolsImpl::runProfile(QString name) {
    if (state_ == LCSState::StateIdle) {
        setState(LCSState::StateBusy);
        setStatus("Run profile");
        LCS::fs::path profilePath = progDirPath_ / "profiles" / name.toStdU16String();
        std::thread thread([this, profilePath = std::move(profilePath)] {
            try {
                setState(LCSState::StateRunning);
                lcs_hint(u8"Try running LCS as Administrator!");
                bool canexit = true;
                auto old_m = LCS::ModOverlay::M_DONE;
                patcher_.run([&](LCS::ModOverlay::Message m) -> bool {
                    if (m != old_m) {
                        setStatus(LCS::ModOverlay::STATUS_MSG[m]);
                    }
                    switch (m) {
                    case LCS::ModOverlay::M_NEED_SAVE:
                        patcher_.save(patcherConfig_);
                        break;
                    case LCS::ModOverlay::M_WAIT_EXIT:
                        canexit = false;
                        break;
                    case LCS::ModOverlay::M_DONE:
                        canexit = true;
                        break;
                    default:
                        break;
                    }
                    return !canexit || this->state_ == LCSState::StateRunning;
                }, profilePath);
                setStatus("Patcher stoped");
                setState(LCSState::StateIdle);
            } catch(std::runtime_error const& error) {
                emit_reportError("Patch league", error);
                setState(LCSState::StateIdle);
            }
        });
        thread.detach();
    }
}

void LCSToolsImpl::stopProfile() {
    if (state_ == LCSState::StateRunning) {
        setState(LCSState::StateStoping);
    }
}

void LCSToolsImpl::startEditMod(QString fileName) {
    if (state_ == LCSState::StateIdle) {
        setState(LCSState::StateBusy);
        setStatus("Edit mod");
        try {
            LCS::fs::path nameStd = fileName.toStdU16String();
            if (auto i = modIndex_->mods().find(nameStd); i != modIndex_->mods().end()) {
                auto const& mod = i->second;
                QJsonArray wads;
                for(auto const& [name, wad]: mod->wads()) {
                    wads.push_back(to_qstring(name));
                }
                emit modEditStarted(fileName,
                                    parseInfoData(fileName, mod->info()),
                                    to_qstring(mod->image()),
                                    wads);
            } else {
                throw std::runtime_error("No such mod found!");
            }
        } catch(std::runtime_error const& error) {
            emit_reportError("Edit mod", error);
        }
        setState(LCSState::StateIdle);
    }
}

void LCSToolsImpl::changeModInfo(QString fileName, QJsonObject infoData, QString image) {
    if (state_ == LCSState::StateIdle) {
        setState(LCSState::StateBusy);
        setStatus("Change mod info");
        try {
            auto const mod = modIndex_->get_mod(fileName.toStdU16String());
            infoData = validateAndCorrect(fileName, infoData);
            mod->change_info(dumpInfoData(infoData));
            mod->change_image(image.toStdU16String());
            image = to_qstring(mod->image());
            emit modInfoChanged(fileName, infoData, image);
        } catch(std::runtime_error const& error) {
            emit_reportError("Change mod info", error);
        }
        setState(LCSState::StateIdle);
    }
}

void LCSToolsImpl::removeModWads(QString fileName, QJsonArray wads) {
    if (state_ == LCSState::StateIdle) {
        setState(LCSState::StateBusy);
        setStatus("Remove mod wads");
        try {
            auto const mod = modIndex_->get_mod(fileName.toStdU16String());
            for(auto wadName: wads) {
                mod->remove_wad(wadName.toString().toStdU16String());
            }
            emit modWadsRemoved(fileName, wads);
        } catch(std::runtime_error const& error) {
            emit_reportError("Remove mod image", error);
        }
        setState(LCSState::StateIdle);
    }
}

void LCSToolsImpl::addModWads(QString fileName, QJsonArray wads, bool removeUnknownNames) {
    if (state_ == LCSState::StateIdle) {
        setState(LCSState::StateBusy);
        setStatus("Add mod wads");
        try {
            auto const& index = wadIndex();
            LCS::WadMakeQueue wadMake(index, removeUnknownNames);
            for(auto wadPath: wads) {
                wadMake.addItem(LCS::fs::path(wadPath.toString().toStdU16String()), LCS::Conflict::Abort);
            }
            auto const mod = modIndex_->get_mod(fileName.toStdU16String());
            auto added = mod->add_wads(wadMake,
                                       *dynamic_cast<LCS::ProgressMulti*>(this),
                                       LCS::Conflict::Abort);
            QJsonArray names;
            for(auto const& wad: added) {
                names.push_back(to_qstring(wad->name()));
            }
            emit modWadsAdded(fileName, names);
        } catch(std::runtime_error const& error) {
            emit_reportError("Add mod wads", error);
        }
        setState(LCSState::StateIdle);
    }
}

/// Interface implementations
void LCSToolsImpl::startItem(LCS::fs::path const& path, std::uint64_t bytes) noexcept {
    auto name = to_qstring(path.filename());
    auto size = QString::number(bytes / 1024.0 / 1024.0, 'f', 2);
    setStatus("Processing " + name + "(" + size + "MB)");
}

void LCSToolsImpl::consumeData(std::uint64_t ammount) noexcept {
    progressDataDone_ += ammount;
    emit progressData((quint64)progressDataDone_);
}

void LCSToolsImpl::finishItem() noexcept {
    progressItemDone_++;
    emit progressItems((quint32)progressItemDone_);
}

void LCSToolsImpl::startMulti(size_t itemCount, std::uint64_t dataTotal) noexcept {
    progressItemDone_ = 0;
    progressDataDone_ = 0;
    emit progressStart((quint32)itemCount, (quint64)dataTotal);
}

void LCSToolsImpl::finishMulti() noexcept {
    emit progressEnd();
}

/// Errorreporting garbage
void LCSToolsImpl::emit_reportError(QString category, std::runtime_error const& error) {
    QString message = to_qstring(error);
    message += to_qstring(LCS::hint_stack_trace());
    QString stack_trace = to_qstring(LCS::error_stack_trace());
    stack_trace += '\n';
    stack_trace += message;
    emit reportError(category, stack_trace, message);
}

