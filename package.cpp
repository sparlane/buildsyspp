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

std::list<PackageDepend>::iterator Package::dependsStart()
{
	return this->depends.begin();
}

std::list<PackageDepend>::iterator Package::dependsEnd()
{
	return this->depends.end();
}

BuildDir *Package::builddir()
{
	return &this->bd;
}

std::ofstream &Package::getLogFile()
{
	if(this->logFile == nullptr) {
		this->logFile =
		    std::make_unique<std::ofstream>(this->builddir()->getPath() + "/build.log");
	}
	return *this->logFile;
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
	return this->getWorld()->getWorkingDir() + "/" + this->relative_fetch_path(location);
}

std::string Package::relative_fetch_path(const std::string &location, bool also_root)
{
	std::string src_path;

	if(location.at(0) == '/' || location.find("dl/") == 0) {
		src_path = location;
	} else {
		auto iter = this->getWorld()->overlaysStart();
		auto end = this->getWorld()->overlaysEnd();
		bool exists(false);

		if(location.at(0) == '.') {
			for(; iter != end; iter++) {
				src_path = *iter + "/" + location;
				if(filesystem::exists(src_path)) {
					exists = true;
					break;
				}
			}
		} else {
			for(; iter != end; iter++) {
				src_path = *iter + "/package/" + this->getName() + "/" + location;
				if(filesystem::exists(src_path)) {
					exists = true;
					break;
				}
				if(also_root) {
					src_path = *iter + "/" + location;
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
	log(this, boost::format{"Processing (%1%)"} % this->file);

	this->build_description.add(
	    std::make_unique<PackageFileUnit>(this->file, this->file_short));

	if(!interfaceSetup(&this->lua)) {
		error("interfaceSetup: Failed");
		exit(-1);
	}

	li_set_package(this);

	this->lua.processFile(this->file);

	return true;
}

bool Package::checkForDependencyLoops()
{
	if(this->visiting) {
		log(this, "Cyclic Dependency");
		return false;
	}

	this->visiting = true;

	auto iter = this->dependsStart();
	auto end = this->dependsEnd();

	for(; iter != end; iter++) {
		if(!(*iter).getPackage()->checkForDependencyLoops()) {
			log(this, "Child failed dependency loop check");
			return false;
		}
	}

	this->visiting = false;

	return true;
}

bool Package::extract_staging(const std::string &dir, std::list<std::string> *done)
{
	{
		auto dIt = done->begin();
		auto dEnd = done->end();

		for(; dIt != dEnd; dIt++) {
			if((*dIt) == this->getNS()->getName() + "," + this->name) {
				return true;
			}
		}
	}

	auto dIt = this->dependsStart();
	auto dEnds = this->dependsEnd();

	for(; dIt != dEnds; dIt++) {
		if(!(*dIt).getPackage()->extract_staging(dir, done)) {
			return false;
		}
	}

	PackageCmd pc(dir, "tar");
	pc.addArg("-xf");
	auto pwd = this->getWorld()->getWorkingDir();
	std::string arg =
	    pwd + "/output/" + this->getNS()->getName() + "/staging/" + this->name + ".tar";
	pc.addArg(arg);

	if(!pc.Run(this)) {
		log(this, "Failed to extract staging_dir");
		return false;
	}

	done->push_back(this->getNS()->getName() + "," + this->name);

	return true;
}

bool Package::extract_install(const std::string &dir, std::list<std::string> *done,
                              bool includeChildren)
{
	{
		auto dIt = done->begin();
		auto dEnd = done->end();

		for(; dIt != dEnd; dIt++) {
			if((*dIt) == this->getNS()->getName() + "," + this->name) {
				return true;
			}
		}
	}

	auto dIt = this->dependsStart();
	auto dEnds = this->dependsEnd();

	if(includeChildren && !this->intercept) {
		for(; dIt != dEnds; dIt++) {
			if(!(*dIt).getPackage()->extract_install(dir, done, includeChildren)) {
				return false;
			}
		}
	}

	auto pwd = this->getWorld()->getWorkingDir();
	if(!this->installFiles.empty()) {
		auto it = this->installFiles.begin();
		auto end = this->installFiles.end();
		for(; it != end; it++) {
			PackageCmd pc(dir, "cp");
			std::string arg =
			    pwd + "/output/" + this->getNS()->getName() + "/install/" + *it;
			pc.addArg(arg);
			pc.addArg(*it);

			if(!pc.Run(this)) {
				log(this, boost::format{"Failed to copy %1% (for install)"} % *it);
				return false;
			}
		}
	} else {
		PackageCmd pc(dir, "tar");
		pc.addArg("-xf");
		std::string arg =
		    pwd + "/output/" + this->getNS()->getName() + "/install/" + this->name + ".tar";
		pc.addArg(arg);
		if(!pc.Run(this)) {
			log(this, "Failed to extract install_dir");
			return false;
		}
	}

	done->push_back(this->getNS()->getName() + "," + this->name);

	return true;
}

bool Package::canBuild()
{
	if(this->isBuilt()) {
		return true;
	}

	auto dIt = this->dependsStart();
	auto dEnds = this->dependsEnd();

	if(dIt != dEnds) {
		for(; dIt != dEnds; dIt++) {
			if(!(*dIt).getPackage()->isBuilt()) {
				return false;
			}
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
		log(P, "Failed to get " + rfile);
		ret = true;
	}
	return ret;
}

void Package::updateBuildInfoHashExisting()
{
	// populate the build.info hash
	std::string build_info_file = this->bd.getPath() + "/.build.info";
	this->buildinfo_hash = hash_file(build_info_file);
	log(this, "Hash: " + this->buildinfo_hash);
}

void Package::updateBuildInfoHash()
{
	// populate the build.info hash
	std::string build_info_file = this->bd.getPath() + "/.build.info.new";
	this->buildinfo_hash = hash_file(build_info_file);
	log(this, "Hash: " + this->buildinfo_hash);
}

std::unique_ptr<BuildUnit> Package::buildInfo()
{
	if(this->isHashingOutput()) {
		std::string info_file = this->bd.getShortPath() + "/.output.info";
		return std::make_unique<OutputInfoFileUnit>(info_file);
	}

	if(this->buildinfo_hash.empty()) {
		log(this, boost::format{"build.info (in %1%) is empty"} % this->bd.getShortPath());
		log(this, "You probably need to build this package");
		return nullptr;
	}

	std::string info_file = this->bd.getShortPath() + "/.build.info";
	return std::make_unique<BuildInfoFileUnit>(info_file, this->buildinfo_hash);
}

void Package::prepareBuildInfo()
{
	if(this->buildInfoPrepared) {
		return;
	}
	// Add the extraction info file
	this->build_description.add(this->Extract.extractionInfo(&this->bd));

	// Add each of our dependencies build info files
	auto dIt = this->dependsStart();
	auto dEnds = this->dependsEnd();

	for(; dIt != dEnds; dIt++) {
		std::unique_ptr<BuildUnit> bi = (*dIt).getPackage()->buildInfo();
		if(bi == nullptr) {
			log(this, "bi is nullptr :(");
			exit(-1);
		}
		this->build_description.add(std::move(bi));
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

	log(this, boost::format{"FF URL: %1%/%2%/%3%/%4%"} % this->getWorld()->fetchFrom() %
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
		log(this, "Could not optimize away building");
	} else {
		log(this, "Build cache used");

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

	std::string fname = this->getWorld()->getWorkingDir() + "/output/" +
	                    this->getNS()->getName() + "/install/" + this->name + ".tar";

	if(!filesystem::exists(fname)) {
		ret = true;
	}

	// Now lets check that the staging file (still) exists
	fname = this->getWorld()->getWorkingDir() + "/output/" + this->getNS()->getName() +
	        "/staging/" + this->name + ".tar";

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

bool Package::prepareBuildDirs()
{
	std::string staging_dir =
	    "output/" + this->getNS()->getName() + "/" + this->name + "/staging";
	log(this, "Generating staging directory ...");

	{ // Clean out the (new) staging/install directories
		std::string pwd = this->getWorld()->getWorkingDir();

		std::string cmd = "rm -fr " + pwd + "/output/" + this->getNS()->getName() + "/" +
		                  this->name + "/new/install/*";
		std::system(cmd.c_str());

		cmd = "rm -fr " + pwd + "/output/" + this->getNS()->getName() + "/" + this->name +
		      "/new/staging/*";
		std::system(cmd.c_str());

		cmd = "rm -fr " + pwd + "/output/" + this->getNS()->getName() + "/" + this->name +
		      "/staging/*";
		std::system(cmd.c_str());
	}

	std::list<std::string> done;
	auto dIt = this->dependsStart();
	auto dEnds = this->dependsEnd();
	for(; dIt != dEnds; dIt++) {
		if(!(*dIt).getPackage()->extract_staging(staging_dir, &done)) {
			return false;
		}
	}
	log(this, boost::format{"Done (%1%)"} % done.size());
	return true;
}

bool Package::extractInstallDepends()
{
	if(this->depsExtraction.empty()) {
		return true;
	}
	// Extract installed files to a given location
	log(this, "Removing old install files ...");
	{
		PackageCmd pc(this->getWorld()->getWorkingDir(), "rm");
		pc.addArg("-fr");
		pc.addArg(this->depsExtraction);
		if(!pc.Run(this)) {
			log(this,
			    boost::format{"Failed to remove %1% (pre-install)"} % this->depsExtraction);
			return false;
		}
	}

	// Create the directory
	filesystem::create_directories(this->depsExtraction);

	log(this, "Extracting installed files from dependencies ...");
	std::list<std::string> done;
	auto dIt = this->dependsStart();
	auto dEnds = this->dependsEnd();
	for(; dIt != dEnds; dIt++) {
		if(!(*dIt).getPackage()->extract_install(this->depsExtraction, &done,
		                                         !this->depsExtractionDirectOnly)) {
			return false;
		}
	}
	log(this, "Dependency install files extracted");
	return true;
}

bool Package::packageNewStaging()
{
	PackageCmd pc(this->bd.getNewStaging(), "tar");
	pc.addArg("-cf");
	std::string arg = this->getWorld()->getWorkingDir() + "/output/" +
	                  this->getNS()->getName() + "/staging/" + this->name + ".tar";
	pc.addArg(arg);
	pc.addArg(".");

	if(!pc.Run(this)) {
		log(this, "Failed to compress staging directory");
		return false;
	}
	return true;
}

bool Package::packageNewInstall()
{
	auto pwd = this->getWorld()->getWorkingDir();
	if(!this->installFiles.empty()) {
		auto it = this->installFiles.begin();
		auto end = this->installFiles.end();
		for(; it != end; it++) {
			log(this, "Copying " + *it + " to install folder");
			PackageCmd pc(this->bd.getNewInstall(), "cp");
			pc.addArg(*it);
			std::string arg =
			    pwd + "/output/" + this->getNS()->getName() + "/install/" + *it;
			pc.addArg(arg);

			if(!pc.Run(this)) {
				log(this, boost::format{"Failed to copy install file (%1%)"} % *it);
				return false;
			}
		}
	} else {
		PackageCmd pc(this->bd.getNewInstall(), "tar");
		pc.addArg("-cf");
		std::string arg =
		    pwd + "/output/" + this->getNS()->getName() + "/install/" + this->name + ".tar";
		pc.addArg(arg);
		pc.addArg(".");

		if(!pc.Run(this)) {
			log(this, "Failed to compress install directory");
			return false;
		}
	}
	return true;
}

void Package::cleanStaging()
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

	auto dIt = this->dependsStart();
	auto dEnds = this->dependsEnd();

	/* Check our dependencies are already built, or build them */
	for(; dIt != dEnds; dIt++) {
		if(!(*dIt).getPackage()->build()) {
			return false;
		}
	}

	if((this->getWorld()->forcedMode() && !this->getWorld()->isForced(this->name))) {
		// Set the build.info hash based on what is currently present
		this->updateBuildInfoHashExisting();
		log(this, "Building suppressed");
		// Just pretend we are built
		this->built = true;
		this->getWorld()->packageFinished(this);
		return true;
	}
	// Create the new extraction.info file
	this->Extract.prepareNewExtractInfo(this, &this->bd);

	// Create the new build.info file
	this->prepareBuildInfo();

	// Check if building is required
	bool sb = this->shouldBuild(locally);

	if(!sb) {
		log(this, "Not required");
		// Already built
		this->built = true;
		this->getWorld()->packageFinished(this);
		return true;
	}
	// Need to check that any packages that need to have been built locally
	// actually have been
	dIt = this->dependsStart();
	for(; dIt != dEnds; dIt++) {
		if((*dIt).getLocally()) {
			log((*dIt).getPackage(), "Build triggered by " + this->getName());
			if(!(*dIt).getPackage()->build(true)) {
				return false;
			}
		}
	}

	// Fetch anything we don't have yet
	if(!this->fetch()->fetch(&this->bd)) {
		log(this, "Fetching failed");
		return false;
	}

	clock_gettime(CLOCK_REALTIME, &start);

	if(this->Extract.extractionRequired(this, &this->bd)) {
		log(this, "Extracting ...");
		if(!this->Extract.extract(this)) {
			return false;
		}
	}

	log(this, "Building ...");
	// Clean new/{staging,install}, Extract the dependency staging directories
	if(!this->prepareBuildDirs()) {
		return false;
	}

	if(!this->extractInstallDepends()) {
		return false;
	}

	auto cIt = this->commands.begin();
	auto cEnd = this->commands.end();

	log(this, "Running Commands");
	for(; cIt != cEnd; cIt++) {
		if(!(*cIt).Run(this)) {
			return false;
		}
	}
	log(this, "Done Commands");

	log(this, "BUILT");

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

	log(this, boost::format{"Built in %1% seconds"} % this->run_secs);

	std::unique_lock<std::mutex> lk(this->lock);
	this->building = false;
	this->built = true;
	this->was_built = true;
	lk.unlock();

	this->getWorld()->packageFinished(this);

	return true;
}
