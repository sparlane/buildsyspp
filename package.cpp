/******************************************************************************
 Copyright 2013 Allied Telesis Labs Ltd. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice,
       this list of conditions and the following disclaimer.

    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*******************************************************************************/

#include "include/buildsys.h"
#include "interface/luainterface.h"

#ifndef TAR_CMD
#define TAR_CMD "/bin/tar"
#endif

Package::Package(NameSpace *_ns, std::string _name, std::string _file_short,
                 std::string _file, std::string _overlay, std::string _pwd)
    : name(std::move(_name)), file(std::move(_file)), file_short(std::move(_file_short)),
      overlay(std::move(_overlay)), pwd(std::move(_pwd)), ns(_ns),
      bd(BuildDir(this->pwd, _ns->getName(), this->name))
{
	std::string prefix = this->ns->getName() + "," + this->name;

	if(this->getWorld()->isQuietly()) {
		auto log_path = boost::format{"%1%/output/%2%/%3%/build.log"} % this->pwd %
		                this->getNS()->getName() % this->name;
		this->logger = std::move(Logger(prefix, log_path.str()));
	} else {
		this->logger = std::move(Logger(prefix));
	}
}

BuildDir *Package::builddir()
{
	return &this->bd;
}

std::string Package::getFeature(const std::string &key)
{
	/* Try the feature prefixed with our package name first */
	try {
		return this->getWorld()->featureMap()->getFeature(this->name + ":" + key);
	} catch(NoKeyException &E) {
		return this->getWorld()->featureMap()->getFeature(key);
	}
}

std::string Package::absolute_fetch_path(const std::string &location)
{
	return this->pwd + "/" + this->relative_fetch_path(location);
}

std::string Package::relative_fetch_path(const std::string &location, bool also_root)
{
	std::string src_path;

	if(location.at(0) == '/' || location.find("dl/") == 0) {
		src_path = location;
	} else {
		bool exists(false);

		if(location.at(0) == '.') {
			for(const auto &ov : this->getWorld()->getOverlays()) {
				src_path = ov + "/" + location;
				if(filesystem::exists(src_path)) {
					exists = true;
					break;
				}
			}
		} else {
			for(const auto &ov : this->getWorld()->getOverlays()) {
				src_path = ov + "/package/" + this->getName() + "/" + location;
				if(filesystem::exists(src_path)) {
					exists = true;
					break;
				}
				if(also_root) {
					src_path = ov + "/" + location;
					if(filesystem::exists(src_path)) {
						exists = true;
						break;
					}
				}
			}
		}
		if(!exists) {
			throw FileNotFoundException(location, this->getName());
		}
	}
	return src_path;
}

std::string Package::getFileHash(const std::string &filename)
{
	std::string hashes_file = this->relative_fetch_path("Digest");
	std::ifstream hashes(hashes_file);

	if(!hashes.is_open()) {
		return "";
	}

	std::string hash;
	std::string line;
	while(std::getline(hashes, line)) {
		auto split = line.find(' ');
		if(split != std::string::npos) {
			std::string fname = line.substr(0, split);
			if(fname == filename) {
				hash = line.substr(split + 1);
				break;
			}
		}
	}

	return hash;
}

void Package::printLabel(std::ostream &out)
{
	out << "[label=\"";

	out << this->getName() << "\\n";
	out << this->getNS()->getName() << "\\n";
	out << "Cmds:" << this->commands.size() << "\\n";
	out << "Time: " << this->run_secs << "s";

	out << "\"]";
}

bool Package::process()
{
	this->log(boost::format{"Processing (%1%)"} % this->file);

	this->build_description.add_package_file(this->file_short, hash_file(this->file));

	if(!interfaceSetup(&this->lua)) {
		this->log("interfaceSetup: Failed");
		exit(-1);
	}

	li_set_package(this);

	this->lua.processFile(this->file);

	return true;
}

/**
 * Extract the staging output for the package into the given directory.
 *
 * @param dir - The directory to extract the staging output into.
 *
 * @returns true if the extraction was successful, false otherwise.
 */
bool Package::extract_staging(const std::string &dir)
{
	PackageCmd pc(dir, TAR_CMD);
	pc.addArg("--no-same-owner");
	pc.addArg("-b");
	pc.addArg("256");
	pc.addArg("-xkf");
	std::string arg = this->pwd + "/output/" + this->getNS()->getName() + "/staging/" +
	                  this->name + ".tar";
	pc.addArg(arg);

	if(!pc.Run(&this->logger)) {
		this->log("Failed to extract staging_dir");
		return false;
	}

	return true;
}

/**
 * Extract the install output for the package into the given directory.
 *
 * @param dir - The directory to extract the install output into.
 *
 * @returns true if the extraction was successful, false otherwise.
 */
bool Package::extract_install(const std::string &dir)
{
	if(!this->installFiles.empty()) {
		auto it = this->installFiles.begin();
		auto end = this->installFiles.end();
		for(; it != end; it++) {
			PackageCmd pc(dir, "cp");
			std::string arg =
			    this->pwd + "/output/" + this->getNS()->getName() + "/install/" + *it;
			pc.addArg(arg);
			pc.addArg(*it);

			if(!pc.Run(&this->logger)) {
				this->log(boost::format{"Failed to copy %1% (for install)"} % *it);
				return false;
			}
		}
	} else {
		PackageCmd pc(dir, TAR_CMD);
		pc.addArg("--no-same-owner");
		pc.addArg("-b");
		pc.addArg("256");
		pc.addArg("-xkf");
		std::string arg = this->pwd + "/output/" + this->getNS()->getName() + "/install/" +
		                  this->name + ".tar";
		pc.addArg(arg);
		if(!pc.Run(&this->logger)) {
			this->log("Failed to extract install_dir");
			return false;
		}
	}

	return true;
}

bool Package::canBuild()
{
	if(this->isBuilt()) {
		return true;
	}

	for(auto &dp : this->depends) {
		if(!dp.getPackage()->isBuilt()) {
			return false;
		}
	}

	return true;
}

static bool ff_file(Package *P, const std::string &hash, const std::string &rfile,
                    const std::string &path, const std::string &fname,
                    const std::string &fext)
{
	bool ret = false;
	std::string url = P->getWorld()->fetchFrom() + "/" + P->getNS()->getName() + "/" +
	                  P->getName() + "/" + hash + "/" + rfile;
	std::string cmd = "wget -q " + url + " -O " + path + "/" + fname + fext;
	int res = std::system(cmd.c_str());
	if(res != 0) {
		P->log("Failed to get " + rfile);
		ret = true;
	}
	return ret;
}

void Package::updateBuildInfoHashExisting()
{
	// populate the build.info hash
	std::string build_info_file = this->bd.getPath() + "/.build.info";
	this->buildinfo_hash = hash_file(build_info_file);
	this->log("Hash: " + this->buildinfo_hash);
}

void Package::updateBuildInfoHash()
{
	// populate the build.info hash
	std::string build_info_file = this->bd.getPath() + "/.build.info.new";
	this->buildinfo_hash = hash_file(build_info_file);
	this->log("Hash: " + this->buildinfo_hash);
}

Package::BuildInfoType Package::buildInfo(std::string *file_path, std::string *hash)
{
	if(this->isHashingOutput()) {
		*file_path = this->bd.getShortPath() + "/.output.info";
		*hash = hash_file(*file_path);
		return BuildInfoType::Output;
	}

	*file_path = this->bd.getShortPath() + "/.build.info";
	*hash = this->buildinfo_hash;
	return BuildInfoType::Build;
}

void Package::prepareBuildInfo()
{
	if(this->buildInfoPrepared) {
		return;
	}
	// Add the extraction info file
	std::string extraction_file_path;
	std::string extraction_file_hash;
	this->Extract.extractionInfo(&this->bd, &extraction_file_path, &extraction_file_hash);
	this->build_description.add_extraction_info_file(extraction_file_path,
	                                                 extraction_file_hash);

	// Add each of our dependencies build info files
	for(auto &dp : this->depends) {
		std::string file_path;
		std::string hash;
		auto type = dp.getPackage()->buildInfo(&file_path, &hash);

		if(hash.empty()) {
			this->log(boost::format{"build info for %1% is empty"} %
			          dp.getPackage()->getName());
			this->log("You probably need to build that package");
			exit(-1);
		}

		if(type == BuildInfoType::Output) {
			this->build_description.add_output_info_file(file_path, hash);
		} else {
			this->build_description.add_build_info_file(file_path, hash);
		}
	}

	// Create the new build info file
	std::string buildInfoFname = this->bd.getPath() + "/.build.info.new";
	std::ofstream _buildInfo(buildInfoFname.c_str());
	this->build_description.print(_buildInfo);
	_buildInfo.close();
	this->updateBuildInfoHash();
	this->buildInfoPrepared = true;
}

void Package::updateBuildInfo(bool updateOutputHash)
{
	// mv the build info file into the regular place
	std::string oldfname = this->bd.getPath() + "/.build.info.new";
	std::string newfname = this->bd.getPath() + "/.build.info";
	rename(oldfname.c_str(), newfname.c_str());

	if(updateOutputHash && this->isHashingOutput()) {
		// Hash the entire new path
		auto cmd = boost::format{"cd %1%; find -type f -exec sha256sum {} \\; | sort -k 2 "
		                         "> %2%/.output.info"} %
		           this->bd.getNewPath() % this->bd.getPath();
		std::system(cmd.str().c_str());
	}
}

bool Package::fetchFrom()
{
	bool ret = false;
	std::string staging_dir = this->getNS()->getStagingDir();
	std::string install_dir = this->getNS()->getInstallDir();
	std::vector<std::array<std::string, 4>> files = {
	    {"usable", staging_dir, this->name, ".tar.ff"},
	    {"staging.tar", staging_dir, this->name, ".tar"},
	    {"install.tar", install_dir, this->name, ".tar"},
	    {"output.info", this->bd.getPath(), ".output", ".info"},
	};

	this->log(boost::format{"FF URL: %1%/%2%/%3%/%4%"} % this->getWorld()->fetchFrom() %
	          this->getNS()->getName() % this->name % this->buildinfo_hash);

	if(!this->isHashingOutput()) {
		files.pop_back();
	}

	for(auto &element : files) {
		ret = ff_file(this, this->buildinfo_hash, element.at(0), element.at(1),
		              element.at(2), element.at(3));
		if(ret) {
			break;
		}
	}

	if(ret) {
		this->log("Could not optimize away building");
	} else {
		this->log("Build cache used");

		this->updateBuildInfo(false);
	}

	return ret;
}

bool Package::shouldBuild(bool locally)
{
	// we need to rebuild if the code is updated
	if(this->codeUpdated) {
		return true;
	}
	// we need to rebuild if installFiles is not empty
	if(!this->installFiles.empty()) {
		return true;
	}

	// lets make sure the install file (still) exists
	bool ret = false;

	std::string fname = this->pwd + "/output/" + this->getNS()->getName() + "/install/" +
	                    this->name + ".tar";

	if(!filesystem::exists(fname)) {
		ret = true;
	}

	// Now lets check that the staging file (still) exists
	fname = this->pwd + "/output/" + this->getNS()->getName() + "/staging/" + this->name +
	        ".tar";

	if(!filesystem::exists(fname)) {
		ret = true;
	}

	auto cmd =
	    boost::format{"cmp -s %1%/.build.info.new %1%/.build.info"} % this->bd.getPath();
	int res = std::system(cmd.str().c_str());

	// if there are changes,
	if(res != 0 || ret) {
		// see if we can grab new staging/install files
		if(!locally && this->canFetchFrom() && this->getWorld()->canFetchFrom()) {
			ret = this->fetchFrom();
		} else {
			// otherwise, make sure we get (re)built
			ret = true;
		}
	}

	if(locally) {
		ret = true;
	}

	return ret;
}

/**
 * Get the set of packages that this package depends on.
 *
 * @param The set to fill with the depended packages.
 * @param include_children - Set true to include the child dependencies of any direct
 * dependencies.
 * @param ignore_intercept - Ignore the intercept setting on the depended package (i.e.
 * include its child dependencies).
 */
void Package::getDependedPackages(std::unordered_set<Package *> *packages,
                                  bool include_children, bool ignore_intercept)
{
	for(auto &dp : this->depends) {
		// This depended package (and therefore all of its dependencies) are already in the
		// set. Don't recurse through them again.
		if(packages->find(dp.getPackage()) != packages->end()) {
			continue;
		}

		packages->insert(dp.getPackage());

		if(include_children && (ignore_intercept || !dp.getPackage()->getIntercept())) {
			dp.getPackage()->getDependedPackages(packages, include_children,
			                                     ignore_intercept);
		}
	}
}

/**
 * Get the set of all packages that this package depends on. Note that this includes
 * the packages that the directly depended packages depend on and so forth.
 *
 * @param The set to fill with all of the depended packages.
 */
void Package::getAllDependedPackages(std::unordered_set<Package *> *packages)
{
	this->getDependedPackages(packages, true, true);
}

static void cleanDir(const std::string &dir)
{
	std::string cmd;

	cmd = "/bin/rm -fr " + dir;
	std::system(cmd.c_str());
	mkdir(dir.c_str(), 0777);
}

bool Package::prepareBuildDirs()
{
	this->log("Generating staging directory ...");

	// Clean out the (new) staging/install directories
	cleanDir(this->bd.getNewInstall());
	cleanDir(this->bd.getNewStaging());
	cleanDir(this->bd.getStaging());

	std::unordered_set<Package *> packages;
	this->getAllDependedPackages(&packages);

	bool use_threads = this->getWorld()->getThreadsLimit() == 0;
	std::list<std::thread> threads;
	std::atomic<bool> result{true};
	for(auto p : packages) {
		if(use_threads) {
			std::thread th([p, this, &result] {
				bool ret = p->extract_staging(this->bd.getStaging());
				if(!ret) {
					result = false;
				}
			});
			threads.push_back(std::move(th));
		} else {
			result = p->extract_staging(this->bd.getStaging());
			if(!result) {
				break;
			}
		}
	}
	for(auto &t : threads) {
		t.join();
	}

	if(result) {
		this->log(boost::format{"Done (%1%)"} % packages.size());
	}

	return result;
}

bool Package::extractInstallDepends()
{
	if(this->depsExtraction.empty()) {
		return true;
	}

	// Extract installed files to a given location
	this->log("Removing old install files ...");
	PackageCmd pc(this->pwd, "/bin/rm");
	pc.addArg("-fr");
	pc.addArg(this->depsExtraction);
	if(!pc.Run(&this->logger)) {
		this->log(boost::format{"Failed to remove %1% (pre-install)"} %
		          this->depsExtraction);
		return false;
	}

	// Create the directory
	filesystem::create_directories(this->depsExtraction);

	this->log("Extracting installed files from dependencies ...");

	std::unordered_set<Package *> packages;
	this->getDependedPackages(&packages, !this->depsExtractionDirectOnly, false);

	bool use_threads = this->getWorld()->getThreadsLimit() == 0;
	std::list<std::thread> threads;
	std::atomic<bool> result{true};
	for(auto p : packages) {
		if(use_threads) {
			std::thread th([p, this, &result] {
				bool ret = p->extract_install(this->depsExtraction);
				if(!ret) {
					result = false;
				}
			});
			threads.push_back(std::move(th));
		} else {
			result = p->extract_install(this->depsExtraction);
			if(!result) {
				break;
			}
		}
	}
	for(auto &t : threads) {
		t.join();
	}

	if(result) {
		this->log("Dependency install files extracted");
	}
	return result;
}

bool Package::packageNewStaging()
{
	PackageCmd pc(this->bd.getNewStaging(), TAR_CMD);
	pc.addArg("--numeric-owner");
	pc.addArg("-b");
	pc.addArg("256");
	pc.addArg("-cf");
	std::string arg = this->pwd + "/output/" + this->getNS()->getName() + "/staging/" +
	                  this->name + ".tar";
	pc.addArg(arg);
	pc.addArg(".");

	if(!pc.Run(&this->logger)) {
		this->log("Failed to compress staging directory");
		return false;
	}
	return true;
}

bool Package::packageNewInstall()
{
	if(!this->installFiles.empty()) {
		auto it = this->installFiles.begin();
		auto end = this->installFiles.end();
		for(; it != end; it++) {
			this->log("Copying " + *it + " to install folder");
			PackageCmd pc(this->bd.getNewInstall(), "cp");
			pc.addArg(*it);
			std::string arg =
			    this->pwd + "/output/" + this->getNS()->getName() + "/install/" + *it;
			pc.addArg(arg);

			if(!pc.Run(&this->logger)) {
				this->log(boost::format{"Failed to copy install file (%1%)"} % *it);
				return false;
			}
		}
	} else {
		PackageCmd pc(this->bd.getNewInstall(), TAR_CMD);
		pc.addArg("--numeric-owner");
		pc.addArg("-b");
		pc.addArg("256");
		pc.addArg("-cf");
		std::string arg = this->pwd + "/output/" + this->getNS()->getName() + "/install/" +
		                  this->name + ".tar";
		pc.addArg(arg);
		pc.addArg(".");

		if(!pc.Run(&this->logger)) {
			this->log("Failed to compress install directory");
			return false;
		}
	}
	return true;
}

void Package::cleanStaging() const
{
	if(this->suppress_remove_staging) {
		return;
	}
	this->bd.cleanStaging();
}

bool Package::build(bool locally)
{
	// clang-format off
	struct timespec start {}, end {};
	// clang-format on

	// Already build, pretend to successfully build
	if((locally && this->was_built) || (!locally && this->isBuilt())) {
		return true;
	}

	/* Check our dependencies are already built, or build them */
	for(auto &dp : this->depends) {
		if(!dp.getPackage()->build()) {
			return false;
		}
	}

	if((this->getWorld()->forcedMode() && !this->getWorld()->isForced(this->name))) {
		// Set the build.info hash based on what is currently present
		this->updateBuildInfoHashExisting();
		this->log("Building suppressed");
		// Just pretend we are built
		this->built = true;
		this->getWorld()->packageFinished(this);
		return true;
	}

	// Hold the lock for the whole build, to avoid multiple running at once
	std::unique_lock<std::mutex> lk(this->lock);

	// Create the new extraction.info file
	this->Extract.prepareNewExtractInfo(this, &this->bd);

	// Create the new build.info file
	this->prepareBuildInfo();

	// Check if building is required
	bool sb = this->shouldBuild(locally);

	if(!sb) {
		this->log("Not required");
		// Already built
		this->built = true;
		this->getWorld()->packageFinished(this);
		return true;
	}
	// Need to check that any packages that need to have been built locally
	// actually have been
	for(auto &dp : this->depends) {
		if(dp.getLocally()) {
			dp.getPackage()->log("Build triggered by " + this->getName());
			if(!dp.getPackage()->build(true)) {
				return false;
			}
		}
	}

	// Fetch anything we don't have yet
	if(!this->fetch()->fetch(&this->bd)) {
		this->log("Fetching failed");
		return false;
	}

	clock_gettime(CLOCK_REALTIME, &start);

	if(this->Extract.extractionRequired(this, &this->bd)) {
		this->log("Extracting ...");
		if(!this->Extract.extract(this)) {
			return false;
		}
	}

	this->log("Building ...");
	// Clean new/{staging,install}, Extract the dependency staging directories
	if(!this->prepareBuildDirs()) {
		return false;
	}

	if(!this->extractInstallDepends()) {
		return false;
	}

	auto cIt = this->commands.begin();
	auto cEnd = this->commands.end();

	this->log("Running Commands");
	for(; cIt != cEnd; cIt++) {
		if(!(*cIt).Run(&this->logger)) {
			return false;
		}
	}
	this->log("Done Commands");

	this->log("BUILT");

	if(!this->packageNewStaging()) {
		return false;
	}

	if(!this->packageNewInstall()) {
		return false;
	}

	this->cleanStaging();

	this->updateBuildInfo();

	clock_gettime(CLOCK_REALTIME, &end);

	this->run_secs = (end.tv_sec - start.tv_sec);

	this->log(boost::format{"Built in %1% seconds"} % this->run_secs);

	this->building = false;
	this->built = true;
	this->was_built = true;
	lk.unlock();

	this->getWorld()->packageFinished(this);

	return true;
}
