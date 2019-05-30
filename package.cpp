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

#include <buildsys.h>
#include "interface/luainterface.h"

std::list < PackageDepend * >::iterator Package::dependsStart()
{
	return this->depends.begin();
}

std::list < PackageDepend * >::iterator Package::dependsEnd()
{
	return this->depends.end();
}

BuildDir *Package::builddir()
{
	if(this->bd == NULL) {
		this->bd = new BuildDir(this);
	}

	return this->bd;
}

FILE *Package::getLogFile()
{
	if(this->logFile == NULL) {
		BuildDir *bd = this->builddir();
		char *fname = NULL;
		asprintf(&fname, "%s/build.log", bd->getPath());
		this->logFile = fopen(fname, "w");
		free(fname);
	}
	return this->logFile;
}

char *Package::absolute_fetch_path(const char *location, bool also_root)
{
	const char *cwd = this->getWorld()->getWorkingDir()->c_str();
	char *src_path;
	char *src_path_tmp = this->relative_fetch_path(location);

	asprintf(&src_path, "%s/%s", cwd, src_path_tmp);

	free(src_path_tmp);
	return src_path;
}

char *Package::relative_fetch_path(const char *location, bool also_root)
{
	char *src_path = NULL;

	if(location[0] == '/' || !strncmp(location, "dl/", 3)) {
		src_path = strdup(location);
	} else {
		string_list::iterator iter = this->getWorld()->overlaysStart();
		string_list::iterator end = this->getWorld()->overlaysEnd();
		struct stat buf;

		if(location[0] == '.') {
			for(; iter != end; iter++) {
				asprintf(&src_path, "%s/%s", (*iter).c_str(), location);
				if(stat(src_path, &buf) == 0) {
					break;
				}
				free(src_path);
				src_path = NULL;
			}
		} else {
			for(; iter != end; iter++) {
				asprintf(&src_path, "%s/package/%s/%s", (*iter).c_str(),
					 this->getName().c_str(), location);
				if(stat(src_path, &buf) == 0) {
					break;
				}
				free(src_path);
				src_path = NULL;
				if(also_root) {
					asprintf(&src_path, "%s/%s", (*iter).c_str(),
						 location);
					if(stat(src_path, &buf) == 0) {
						break;
					}
					free(src_path);
					src_path = NULL;
				}
			}
		}
		if(src_path == NULL) {
			throw FileNotFoundException(location, this->getName().c_str());
		}
	}
	return src_path;
}


char *Package::getFileHash(const char *filename)
{
	char *ret = NULL;
	char *hashes_file = this->relative_fetch_path("Digest");
	FILE *hashes = fopen(hashes_file, "r");
	if(hashes != NULL) {
		char buf[1024];
		memset(buf, 0, 1024);
		while(fgets(buf, sizeof(buf), hashes) > 0) {
			char *hash = strchr(buf, ' ');
			if(hash != NULL) {
				*hash = '\0';
				hash++;
				char *term = strchr(hash, '\n');
				if(term) {
					*term = '\0';
				}
			}
			if(strcmp(buf, filename) == 0) {
				ret = strdup(hash);
				break;
			}
		}
		fclose(hashes);
	}
	free(hashes_file);
	return ret;
}

void Package::resetBD()
{
	if(this->bd != NULL) {
		delete this->bd;
		this->bd = new BuildDir(this);
	}
}

void Package::printLabel(std::ostream & out)
{
	out << "[label=\"";

	out << this->getName() << "\\n";
	out << this->getNS()->getName().c_str() << "\\n";
	out << "Cmds:" << this->commands.size() << "\\n";
	out << "Time: " << this->run_secs << "s";

	out << "\"]";
}

bool Package::process()
{
	log(this, "Processing (%s)", this->file.c_str());

	this->
	    build_description->add(new
				   PackageFileUnit(this->file.c_str(),
						   this->file_short.c_str()));

	if(!interfaceSetup(this->lua)) {
		error("interfaceSetup: Failed");
		exit(-1);
	}

	li_set_package(this->lua->luaState(), this);

	this->lua->processFile(file.c_str());

	return true;
}

bool Package::checkForDependencyLoops()
{
	if(this->visiting == true) {
		log(this, "Cyclic Dependency");
		return false;
	}

	this->visiting = true;

	auto iter = this->dependsStart();
	auto end = this->dependsEnd();

	for(; iter != end; iter++) {
		if(!(*iter)->getPackage()->checkForDependencyLoops()) {
			log(this, "Child failed dependency loop check");
			return false;
		}
	}

	this->visiting = false;

	return true;
}

bool Package::extract_staging(const char *dir, std::list < std::string > *done)
{
	{
		std::list < std::string >::iterator dIt = done->begin();
		std::list < std::string >::iterator dEnd = done->end();

		for(; dIt != dEnd; dIt++) {
			if((*dIt).compare(this->getNS()->getName() + "," + this->name) == 0)
				return true;
		}
	}

	auto dIt = this->dependsStart();
	auto dEnds = this->dependsEnd();

	for(; dIt != dEnds; dIt++) {
		if(!(*dIt)->getPackage()->extract_staging(dir, done))
			return false;
	}

	const char *pwd = this->getWorld()->getWorkingDir()->c_str();

	if(this->bd != NULL) {
		std::unique_ptr < PackageCmd > pc(new PackageCmd(dir, "pax"));
		pc->addArg("-rf");
		pc->addArgFmt("%s/output/%s/staging/%s.tar.bz2", pwd,
			      this->getNS()->getName().c_str(), this->name.c_str());
		pc->addArg("-p");
		pc->addArg("p");

		if(!pc->Run(this)) {
			log(this, "Failed to extract staging_dir");
			return false;
		}
	}

	done->push_back(this->getNS()->getName() + "," + this->name);

	return true;
}

bool Package::extract_install(const char *dir, std::list < std::string > *done)
{
	{
		std::list < std::string >::iterator dIt = done->begin();
		std::list < std::string >::iterator dEnd = done->end();

		for(; dIt != dEnd; dIt++) {
			if((*dIt).compare(this->getNS()->getName() + "," + this->name) == 0)
				return true;
		}
	}

	auto dIt = this->dependsStart();
	auto dEnds = this->dependsEnd();

	if(!this->intercept) {
		for(; dIt != dEnds; dIt++) {
			if(!(*dIt)->getPackage()->extract_install(dir, done))
				return false;
		}
	}

	const char *pwd = this->getWorld()->getWorkingDir()->c_str();

	if(this->bd != NULL) {
		if(!this->installFiles.empty()) {
			std::list < std::string >::iterator it = this->installFiles.begin();
			std::list < std::string >::iterator end = this->installFiles.end();
			for(; it != end; it++) {
				std::unique_ptr < PackageCmd >
				    pc(new PackageCmd(dir, "cp"));
				pc->addArgFmt("%s/output/%s/install/%s", pwd,
					      this->getNS()->getName().c_str(),
					      (*it).c_str());
				pc->addArg(*it);

				if(!pc->Run(this)) {
					log(this, "Failed to copy %s (for install)\n",
					    (*it).c_str());
					return false;
				}
			}
		} else {
			std::unique_ptr < PackageCmd > pc(new PackageCmd(dir, "pax"));
			pc->addArg("-rf");
			pc->addArgFmt("%s/output/%s/install/%s.tar.bz2", pwd,
				      this->getNS()->getName().c_str(), this->name.c_str());
			pc->addArg("-p");
			pc->addArg("p");
			if(!pc->Run(this)) {
				log(this, "Failed to extract install_dir\n");
				return false;
			}
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
			if(!(*dIt)->getPackage()->isBuilt())
				return false;
		}
	}
	return true;
}

bool Package::isBuilt()
{
	bool ret = false;
	pthread_mutex_lock(&this->lock);
	ret = this->built;
	pthread_mutex_unlock(&this->lock);
	return ret;
}

bool Package::isBuilding()
{
	bool ret = false;
	pthread_mutex_lock(&this->lock);
	ret = this->building;
	pthread_mutex_unlock(&this->lock);
	return ret;
}

void Package::setBuilding()
{
	pthread_mutex_lock(&this->lock);
	this->building = true;
	pthread_mutex_unlock(&this->lock);
}

static bool ff_file(Package * P, const char *hash, const char *rfile, const char *path,
		    const char *fname, const char *fext)
{
	bool ret = false;
	char *url = NULL;
	asprintf(&url, "%s/%s/%s/%s/%s", P->getWorld()->fetchFrom().c_str(),
		 P->getNS()->getName().c_str(), P->getName().c_str(), hash, rfile);
	char *cmd = NULL;
	asprintf(&cmd, "wget -q %s -O %s/%s%s", url, path, fname, fext);
	int res = system(cmd);
	if(res != 0) {
		log(P, "Failed to get %s", rfile);
		ret = true;
	}
	free(cmd);
	free(url);
	return ret;
}

void Package::updateBuildInfoHashExisting()
{
	// populate the build.info hash
	char *build_info_file = NULL;
	asprintf(&build_info_file, "%s/.build.info", this->bd->getPath());
	char *hash = hash_file(build_info_file);
	if(hash != NULL) {
		this->buildinfo_hash = std::string(hash);
	}
	log(this, "Hash: %s", hash);
	free(hash);
	free(build_info_file);
}

void Package::updateBuildInfoHash()
{
	// populate the build.info hash
	char *build_info_file = NULL;
	asprintf(&build_info_file, "%s/.build.info.new", this->bd->getPath());
	char *hash = hash_file(build_info_file);
	this->buildinfo_hash = std::string(hash);
	log(this, "Hash: %s", hash);
	free(hash);
	free(build_info_file);
}

BuildUnit *Package::buildInfo()
{
	char *Info_file = NULL;
	BuildUnit *res;
	if(this->isHashingOutput()) {
		asprintf(&Info_file, "%s/.output.info", this->bd->getShortPath());
		res = new OutputInfoFileUnit(Info_file);
	} else {
		if(this->buildinfo_hash.compare("") == 0) {
			log(this, "build.info (in %s) is empty", this->bd->getShortPath());
			log(this, "You probably need to build this package");
			return NULL;
		}
		asprintf(&Info_file, "%s/.build.info", this->bd->getShortPath());
		res = new BuildInfoFileUnit(Info_file, this->buildinfo_hash);
	}
	free(Info_file);
	return res;
}

void Package::prepareBuildInfo()
{
	if(this->buildInfoPrepared) {
		return;
	}
	// Add the extraction info file
	this->build_description->add(this->Extract->extractionInfo(this, this->bd));

	// Add each of our dependencies build info files
	auto dIt = this->dependsStart();
	auto dEnds = this->dependsEnd();

	for(; dIt != dEnds; dIt++) {
		BuildUnit *bi = (*dIt)->getPackage()->buildInfo();
		if(!bi) {
			log(this, "bi is NULL :(");
			exit(-1);
		}
		this->build_description->add(bi);
	}

	// Create the new build info file
	char *buildInfoFname = NULL;
	asprintf(&buildInfoFname, "%s/.build.info.new", this->bd->getPath());
	std::ofstream buildInfo(buildInfoFname);
	this->build_description->print(buildInfo);
	buildInfo.close();
	free(buildInfoFname);
	this->updateBuildInfoHash();
	this->buildInfoPrepared = true;
}

void Package::updateBuildInfo(bool updateOutputHash)
{
	// mv the build info file into the regular place
	char *oldfname = NULL;
	char *newfname = NULL;
	asprintf(&oldfname, "%s/.build.info.new", this->bd->getPath());
	asprintf(&newfname, "%s/.build.info", this->bd->getPath());
	rename(oldfname, newfname);
	free(oldfname);
	free(newfname);

	if(updateOutputHash && this->isHashingOutput()) {
		// Hash the entire new path
		char *cmd = NULL;
		asprintf(&cmd,
			 "cd %s; find -type f -exec sha256sum {} \\; | sort -k 2 > %s/.output.info",
			 this->bd->getNewPath(), this->bd->getPath());
		system(cmd);
		free(cmd);
	}
}

bool Package::fetchFrom()
{
	bool ret = false;
	char *staging_dir = strdup(this->getNS()->getStagingDir().c_str());
	char *install_dir = strdup(this->getNS()->getInstallDir().c_str());
	const char *files[4][4] = {
		{ "usable", staging_dir, this->name.c_str(), ".tar.bz2.ff" },
		{ "staging.tar.bz2", staging_dir, this->name.c_str(), ".tar.bz2" },
		{ "install.tar.bz2", install_dir, this->name.c_str(), ".tar.bz2" },
		{ "output.info", this->bd->getPath(), ".output", ".info" }
	};
	log(this, "FF URL: %s/%s/%s/%s", this->getWorld()->fetchFrom().c_str(),
	    this->getNS()->getName().c_str(), this->name.c_str(),
	    this->buildinfo_hash.c_str());

	int count = 3;
	if(this->isHashingOutput()) {
		count = 4;
	}

	for(int i = 0; !ret && i < count; i++) {
		ret =
		    ff_file(this, this->buildinfo_hash.c_str(), files[i][0], files[i][1],
			    files[i][2], files[i][3]);
	}

	if(ret) {
		log(this, "Could not optimize away building");
	} else {
		log(this, "Build cache used");

		this->updateBuildInfo(false);
	}
	free(staging_dir);
	free(install_dir);
	return ret;
}

bool Package::shouldBuild(bool locally)
{
	// we need to rebuild if the code is updated
	if(this->codeUpdated)
		return true;
	// we need to rebuild if installFiles is not empty
	if(!this->installFiles.empty())
		return true;

	const char *pwd = this->getWorld()->getWorkingDir()->c_str();
	// lets make sure the install file (still) exists
	bool ret = false;
	char *fname = NULL;
	asprintf(&fname, "%s/output/%s/install/%s.tar.bz2", pwd,
		 this->getNS()->getName().c_str(), this->name.c_str());
	FILE *f = fopen(fname, "r");
	if(f == NULL) {
		ret = true;
	} else {
		fclose(f);
	}
	free(fname);
	fname = NULL;
	// Now lets check that the staging file (still) exists
	asprintf(&fname, "%s/output/%s/staging/%s.tar.bz2", pwd,
		 this->getNS()->getName().c_str(), this->name.c_str());
	f = fopen(fname, "r");
	if(f == NULL) {
		ret = true;
	} else {
		fclose(f);
	}
	free(fname);
	fname = NULL;

	char *cmd = NULL;
	asprintf(&cmd, "cmp -s %s/.build.info.new %s/.build.info", this->bd->getPath(),
		 this->bd->getPath());
	int res = system(cmd);
	free(cmd);
	cmd = NULL;

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
	char *staging_dir = NULL;
	asprintf(&staging_dir, "output/%s/%s/staging", this->getNS()->getName().c_str(),
		 this->name.c_str());
	log(this, "Generating staging directory ...");

	{			// Clean out the (new) staging/install directories
		char *cmd = NULL;
		const char *pwd = this->getWorld()->getWorkingDir()->c_str();
		asprintf(&cmd, "rm -fr %s/output/%s/%s/new/install/*", pwd,
			 this->getNS()->getName().c_str(), this->name.c_str());
		system(cmd);
		free(cmd);
		cmd = NULL;
		asprintf(&cmd, "rm -fr %s/output/%s/%s/new/staging/*", pwd,
			 this->getNS()->getName().c_str(), this->name.c_str());
		system(cmd);
		free(cmd);
		cmd = NULL;
		asprintf(&cmd, "rm -fr %s/output/%s/%s/staging/*", pwd,
			 this->getNS()->getName().c_str(), this->name.c_str());
		system(cmd);
		free(cmd);
	}

	std::list < std::string > *done = new std::list < std::string > ();
	auto dIt = this->dependsStart();
	auto dEnds = this->dependsEnd();
	for(; dIt != dEnds; dIt++) {
		if(!(*dIt)->getPackage()->extract_staging(staging_dir, done))
			return false;
	}
	log(this, "Done (%d)", done->size());
	delete done;
	free(staging_dir);
	staging_dir = NULL;
	return true;
}

bool Package::extractInstallDepends()
{
	if(this->depsExtraction == NULL) {
		return true;
	}
	// Extract installed files to a given location
	log(this, "Removing old install files ...");
	{
		std::unique_ptr < PackageCmd >
		    pc(new PackageCmd(this->getWorld()->getWorkingDir()->c_str(), "rm"));
		pc->addArg("-fr");
		pc->addArg(this->depsExtraction);
		if(!pc->Run(this)) {
			log(this,
			    "Failed to remove %s (pre-install)", this->depsExtraction);
			return false;
		}
	}

	// Create the directory
	{
		int res = mkdir(this->depsExtraction, 0700);
		if((res < 0) && (errno != EEXIST)) {
			error(this->depsExtraction);
			return false;
		}
	}

	log(this, "Extracting installed files from dependencies ...");
	std::list < std::string > *done = new std::list < std::string > ();
	auto dIt = this->dependsStart();
	auto dEnds = this->dependsEnd();
	for(; dIt != dEnds; dIt++) {
		if(!(*dIt)->getPackage()->extract_install(this->depsExtraction, done))
			return false;
	}
	delete done;
	log(this, "Dependency install files extracted");
	return true;
}

bool Package::packageNewStaging()
{
	std::unique_ptr < PackageCmd > pc(new PackageCmd(this->bd->getNewStaging(), "pax"));
	pc->addArg("-x");
	pc->addArg("cpio");
	pc->addArg("-wf");
	pc->addArgFmt("%s/output/%s/staging/%s.tar.bz2",
		      this->getWorld()->getWorkingDir()->c_str(),
		      this->getNS()->getName().c_str(), this->name.c_str());
	pc->addArg(".");

	if(!pc->Run(this)) {
		log(this, "Failed to compress staging directory");
		return false;
	}
	return true;
}

bool Package::packageNewInstall()
{
	const char *pwd = this->getWorld()->getWorkingDir()->c_str();
	if(!this->installFiles.empty()) {
		std::list < std::string >::iterator it = this->installFiles.begin();
		std::list < std::string >::iterator end = this->installFiles.end();
		for(; it != end; it++) {
			log(this, ("Copying " + *it + " to install folder").c_str());
			std::unique_ptr < PackageCmd >
			    pc(new PackageCmd(this->bd->getNewInstall(), "cp"));
			pc->addArg(*it);
			pc->addArgFmt("%s/output/%s/install/%s", pwd,
				      this->getNS()->getName().c_str(), (*it).c_str());

			if(!pc->Run(this)) {
				log(this, "Failed to copy install file (%s) ",
				    (*it).c_str());
				return false;
			}
		}
	} else {
		std::unique_ptr < PackageCmd >
		    pc(new PackageCmd(this->bd->getNewInstall(), "pax"));
		pc->addArg("-x");
		pc->addArg("cpio");
		pc->addArg("-wf");
		pc->addArgFmt("%s/output/%s/install/%s.tar.bz2", pwd,
			      this->getNS()->getName().c_str(), this->name.c_str());
		pc->addArg(".");

		if(!pc->Run(this)) {
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
	this->bd->cleanStaging();
}

bool Package::build(bool locally)
{
	struct timespec start, end;

	// Already build, pretend to successfully build
	if((locally && this->wasBuilt()) || (!locally && this->isBuilt())) {
		return true;
	}

	auto dIt = this->dependsStart();
	auto dEnds = this->dependsEnd();

	/* Check our dependencies are already built, or build them */
	for(; dIt != dEnds; dIt++) {
		if(!(*dIt)->getPackage()->build())
			return false;
	}

	if((this->getWorld()->forcedMode() && !this->getWorld()->isForced(this->name))) {
		// Set the build.info hash based on what is currently present
		this->updateBuildInfoHashExisting();
		pthread_mutex_lock(&this->lock);
		log(this, "Building suppressed");
		// Just pretend we are built
		this->built = true;
		pthread_mutex_unlock(&this->lock);
		this->getWorld()->packageFinished(this);
		return true;
	}
	// Create the new extraction.info file
	this->Extract->prepareNewExtractInfo(this, this->bd);

	// Create the new build.info file
	this->prepareBuildInfo();

	// Check if building is required
	bool sb = this->shouldBuild(locally);

	if(!sb) {
		pthread_mutex_lock(&this->lock);
		log(this, "Not required");
		// Already built
		this->built = true;
		pthread_mutex_unlock(&this->lock);
		this->getWorld()->packageFinished(this);
		return true;
	}
	// Need to check that any packages that need to have been built locally
	// actually have been
	dIt = this->dependsStart();
	for(; dIt != dEnds; dIt++) {
		if((*dIt)->getLocally()) {
			log((*dIt)->getPackage(), "Build triggered by %s",
			    this->getName().c_str());
			if(!(*dIt)->getPackage()->build(true))
				return false;
		}
	}

	// Fetch anything we don't have yet
	if(!this->fetch()->fetch(bd)) {
		log(this, "Fetching failed");
		return false;
	}

	clock_gettime(CLOCK_REALTIME, &start);

	if(this->Extract->extractionRequired(this, this->bd)) {
		log(this, "Extracting ...");
		if(!this->Extract->extract(this, this->bd)) {
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

	std::list < PackageCmd * >::iterator cIt = this->commands.begin();
	std::list < PackageCmd * >::iterator cEnd = this->commands.end();

	log(this, "Running Commands");
	for(; cIt != cEnd; cIt++) {
		if(!(*cIt)->Run(this))
			return false;
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

	log(this, "Built in %d seconds", this->run_secs);
	pthread_mutex_lock(&this->lock);
	this->building = false;
	this->built = true;
	this->was_built = true;
	pthread_mutex_unlock(&this->lock);
	this->getWorld()->packageFinished(this);

	return true;
}
