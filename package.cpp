/****************************************************************************************************************************
 Copyright 2013 Allied Telesis Labs Ltd. All rights reserved.
 
Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

    2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the
distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
***************************************************************************************************************************/

#include <buildsys.h>

std::list < Package * >::iterator Package::dependsStart()
{
	return this->depends.begin();
}

std::list < Package * >::iterator Package::dependsEnd()
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

char *Package::absolute_fetch_path(const char *location)
{
	char *src_path = NULL;
	char *cwd = getcwd(NULL, 0);
	if(location[0] == '/' || !strncmp(location, "dl/", 3) || location[0] == '.') {
		asprintf(&src_path, "%s/%s", cwd, location);
	} else {
		asprintf(&src_path, "%s/%s/%s/%s", cwd, this->getOverlay().c_str(),
			 this->getName().c_str(), location);
	}
	free(cwd);
	return src_path;
}

char *Package::relative_fetch_path(const char *location)
{
	char *src_path = NULL;
	if(location[0] == '/' || !strncmp(location, "dl/", 3) || location[0] == '.') {
		src_path = strdup(location);
	} else {
		asprintf(&src_path, "%s/%s/%s", this->getOverlay().c_str(),
			 this->getName().c_str(), location);
	}
	return src_path;
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
	out << "Cmds:" << this->commands.size() << "\\n";
	out << "Time: " << this->run_secs << "s";

	out << "\"]";
}

bool Package::process()
{
	if(this->visiting == true) {
		log(this, (char *) "dependency loop!!!");
		return false;
	}
	if(this->processed == true)
		return true;

	this->visiting = true;

	log(this, (char *) "Processing (%s)", this->file.c_str());

	this->build_description->add(new PackageFileUnit(this->file.c_str()));

	WORLD->getLua()->setGlobal(std::string("P"), this);

	WORLD->getLua()->processFile(file.c_str());

	this->processed = true;

	std::list < Package * >::iterator iter = this->depends.begin();
	std::list < Package * >::iterator end = this->depends.end();

	for(; iter != end; iter++) {
		if(!(*iter)->process()) {
			throw CustomException("dependency failure");
			return false;
		}
	}

	this->visiting = false;
	return true;
}

bool Package::extract()
{
	if(this->extracted) {
		return true;
	}

	if(this->bd) {
		// Dont bother extracting if we are running in forced mode, and this package isn't forced
		if(!(WORLD->forcedMode() && !WORLD->isForced(this->getName()))) {
			// Create the new extraction info file
			char *exinfoFname = NULL;
			asprintf(&exinfoFname, "%s/.extraction.info.new",
				 this->bd->getPath());
			std::ofstream exInfo(exinfoFname);
			this->Extract->print(exInfo);
			free(exinfoFname);

			char *cmd = NULL;
			asprintf(&cmd, "cmp -s %s/.extraction.info.new %s/.extraction.info",
				 this->bd->getPath(), this->bd->getPath());
			int res = system(cmd);
			free(cmd);
			cmd = NULL;

			// if there are changes,
			if(res != 0 || this->codeUpdated) {	// Extract our source code
				log(this, (char *) "Extracting sources and patching");
				this->Extract->extract(this, this->bd);
				//this->setCodeUpdated();
			}
			// mv the file into the regular place
			asprintf(&cmd, "mv %s/.extraction.info.new %s/.extraction.info",
				 this->bd->getPath(), this->bd->getPath());
			system(cmd);
			free(cmd);
		}
	}

	std::list < Package * >::iterator iter = this->depends.begin();
	std::list < Package * >::iterator end = this->depends.end();

	for(; iter != end; iter++) {
		if(!(*iter)->extract()) {
			log(this, (char *) "dependency extract failed");
			return false;
		}
	}

	this->extracted = true;
	return true;
}

bool Package::extract_staging(const char *dir, std::list < std::string > *done)
{
	{
		std::list < std::string >::iterator dIt = done->begin();
		std::list < std::string >::iterator dEnd = done->end();

		for(; dIt != dEnd; dIt++) {
			if((*dIt).compare(this->name) == 0)
				return true;
		}
	}

	std::list < Package * >::iterator dIt = this->depends.begin();
	std::list < Package * >::iterator dEnds = this->depends.end();

	for(; dIt != dEnds; dIt++) {
		if(!(*dIt)->extract_staging(dir, done))
			return false;
	}

	char *pwd = getcwd(NULL, 0);

	if(this->bd != NULL) {
		std::unique_ptr < PackageCmd > pc(new PackageCmd(dir, "pax"));
		pc->addArg("-rf");
		pc->addArgFmt("%s/output/%s/staging/%s.tar.bz2", pwd,
			      this->getNS()->getName().c_str(), this->name.c_str());
		pc->addArg("-p");
		pc->addArg("p");

		if(!pc->Run(this)) {
			log(this, (char *) "Failed to extract staging_dir");
			return false;
		}
	}
	free(pwd);

	done->push_back(this->name);

	return true;
}

bool Package::extract_install(const char *dir, std::list < std::string > *done)
{
	{
		std::list < std::string >::iterator dIt = done->begin();
		std::list < std::string >::iterator dEnd = done->end();

		for(; dIt != dEnd; dIt++) {
			if((*dIt).compare(this->name) == 0)
				return true;
		}
	}

	std::list < Package * >::iterator dIt = this->depends.begin();
	std::list < Package * >::iterator dEnds = this->depends.end();

	if(!this->intercept) {
		for(; dIt != dEnds; dIt++) {
			if(!(*dIt)->extract_install(dir, done))
				return false;
		}
	}

	char *pwd = getcwd(NULL, 0);

	if(this->bd != NULL) {
		if(this->installFile) {
			std::unique_ptr < PackageCmd > pc(new PackageCmd(dir, "cp"));
			pc->addArgFmt("%s/output/%s/install/%s", pwd,
				      this->getNS()->getName().c_str(), this->installFile);
			pc->addArg(this->installFile);

			if(!pc->Run(this)) {
				log(this, "Failed to copy %s (for install)\n",
				    this->installFile);
				return false;
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
	free(pwd);

	done->push_back(this->name);

	return true;
}

bool Package::canBuild()
{
	if(this->isBuilt()) {
		return true;
	}

	std::list < Package * >::iterator dIt = this->depends.begin();
	std::list < Package * >::iterator dEnds = this->depends.end();

	if(dIt != dEnds) {
		for(; dIt != dEnds; dIt++) {
			if(!(*dIt)->isBuilt())
				return false;
		}
	}
	return true;
}

bool Package::isBuilt()
{
	bool ret = false;
#ifdef UNDERSCORE
	// lock
	us_mutex_lock(this->lock);
#endif
	ret = this->built;
#ifdef UNDERSCORE
	// unlock
	us_mutex_unlock(this->lock);
#endif
	return ret;
}

bool Package::isBuilding()
{
	bool ret = false;
#ifdef UNDERSCORE
	// lock
	us_mutex_lock(this->lock);
#endif
	ret = this->building;
#ifdef UNDERSCORE
	// unlock
	us_mutex_unlock(this->lock);
#endif
	return ret;
}

void Package::setBuilding()
{
#ifdef UNDERSCORE
	// lock
	us_mutex_lock(this->lock);
#endif
	this->building = true;
#ifdef UNDERSCORE
	// unlock
	us_mutex_unlock(this->lock);
#endif
}

bool Package::shouldBuild()
{
	// we dont need to build if we don't have a build directory
	if(this->bd == NULL)
		return false;
	// Add the extraction info file
	char *extractionInfoFname = NULL;
	asprintf(&extractionInfoFname, "%s/.extraction.info", this->bd->getShortPath());
	this->build_description->add(new ExtractionInfoFileUnit(extractionInfoFname));
	free(extractionInfoFname);

	// Add each of our dependencies build info files
	std::list < Package * >::iterator dIt = this->depends.begin();
	std::list < Package * >::iterator dEnds = this->depends.end();

	if(dIt != dEnds) {
		for(; dIt != dEnds; dIt++) {
			if((*dIt)->bd != NULL) {
				char *Info_file = NULL;
				if((*dIt)->isHashingOutput()) {
					asprintf(&Info_file, "%s/.output.info",
						 (*dIt)->bd->getShortPath());
					this->build_description->add(new
								     OutputInfoFileUnit
								     (Info_file));
				} else {
					asprintf(&Info_file, "%s/.build.info",
						 (*dIt)->bd->getShortPath());
					this->build_description->add(new
								     BuildInfoFileUnit
								     (Info_file));
				}
				free(Info_file);
			}
		}
	}
	// Create the new build info file
	char *buildInfoFname = NULL;
	asprintf(&buildInfoFname, "%s/.build.info.new", this->bd->getPath());
	std::ofstream buildInfo(buildInfoFname);
	this->build_description->print(buildInfo);
	free(buildInfoFname);

	// we need to rebuild if the code is updated
	if(this->codeUpdated)
		return true;
	// we need to rebuild if installFile is set
	if(this->installFile)
		return true;

	char *pwd = getcwd(NULL, 0);
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
	if(res != 0 || (ret && !this->codeUpdated)) {
		// see if we can grab new staging/install files
		if(this->canFetchFrom() && WORLD->canFetchFrom()) {
			ret = false;
			const char *ffrom = WORLD->fetchFrom().c_str();
			char *build_info_file = NULL;
			asprintf(&build_info_file, "%s/.build.info.new",
				 this->bd->getPath());
			char *hash = hash_file(build_info_file);
			char *url = NULL;
			log(this, "FF URL: %s/%s/%s/%s", ffrom,
			    this->getNS()->getName().c_str(), this->name.c_str(), hash);
			if(!ret) {
				asprintf(&url, "%s/%s/%s/%s/usable", ffrom,
					 this->getNS()->getName().c_str(),
					 this->name.c_str(), hash);
				// try wget the file
				char *cmd = NULL;
				asprintf(&cmd,
					 "wget -q %s -O output/%s/staging/%s.tar.bz2.ff\n",
					 url, this->getNS()->getName().c_str(),
					 this->name.c_str());
				res = system(cmd);
				if(res != 0) {
					log(this, "Failed to get usable");
					ret = true;
				}
			}
			if(!ret) {
				asprintf(&url, "%s/%s/%s/%s/staging.tar.bz2", ffrom,
					 this->getNS()->getName().c_str(),
					 this->name.c_str(), hash);
				// try wget the file
				asprintf(&cmd,
					 "wget -q %s -O output/%s/staging/%s.tar.bz2\n",
					 url, this->getNS()->getName().c_str(),
					 this->name.c_str());
				res = system(cmd);
				if(res != 0) {
					log(this, "Failed to get staging.tar.bz2");
					ret = true;
				}
			}
			if(!ret) {
				asprintf(&url, "%s/%s/%s/%s/install.tar.bz2", ffrom,
					 this->getNS()->getName().c_str(),
					 this->name.c_str(), hash);
				// try wget the file
				asprintf(&cmd,
					 "wget -q %s -O output/%s/install/%s.tar.bz2\n",
					 url, this->getNS()->getName().c_str(),
					 this->name.c_str());
				res = system(cmd);
				if(res != 0) {
					log(this, "Failed to get install.tar.bz2");
					ret = true;
				}
			}
			free(cmd);
			if(!ret && this->isHashingOutput()) {
				asprintf(&url, "%s/%s/%s/%s/output.info", ffrom,
					 this->getNS()->getName().c_str(),
					 this->name.c_str(), hash);
				// try wget the output hash file
				asprintf(&cmd, "wget -q %s -O %s/.output.info\n", url,
					 this->bd->getPath());
				res = system(cmd);
				if(res != 0) {
					log(this, "Failed to get output.info");
					ret = true;
				}
				free(cmd);
			}
			free(hash);
			if(ret) {
				log(this, "Could not optimize away building");
			} else {
				log(this, "Build cache used %s", url);

				char *cmd = NULL;
				// mv the build info file into the regular place (faking that we have built this package)
				asprintf(&cmd, "mv %s/.build.info.new %s/.build.info",
					 this->bd->getPath(), this->bd->getPath());
				system(cmd);
				free(cmd);
			}
			free(url);
		} else {
			// otherwise, make sure we get (re)built
			ret = true;
		}
	}

	free(pwd);

	return ret;
}

bool Package::build()
{
	struct timespec start, end;

	if(this->isBuilt()) {
		return true;
	}

	std::list < Package * >::iterator dIt = this->depends.begin();
	std::list < Package * >::iterator dEnds = this->depends.end();

	if(dIt != dEnds) {
		for(; dIt != dEnds; dIt++) {
			if(!(*dIt)->build())
				return false;
			if((*dIt)->wasBuilt()) {
				std::cout << "Dependency: " << (*dIt)->getName() <<
				    " was built" << std::endl;
			}
		}
	}

	bool sb = this->shouldBuild();

	if((WORLD->forcedMode() && !WORLD->isForced(this->name)) || (!sb)) {
#ifdef UNDERSCORE
		// lock
		us_mutex_lock(this->lock);
#endif
		log(this, "Not required");
		// Just pretend we are built
		this->built = true;
#ifdef UNDERSCORE
		// unlock
		us_mutex_unlock(this->lock);
#endif
		WORLD->packageFinished(this);
		return true;
	}

	clock_gettime(CLOCK_REALTIME, &start);

	char *pwd = getcwd(NULL, 0);

	if(this->bd != NULL) {
		log(this, (char *) "Building ...");
		// Extract the dependency staging directories
		char *staging_dir = NULL;
		asprintf(&staging_dir, "output/%s/%s/staging",
			 this->getNS()->getName().c_str(), this->name.c_str());
		log(this, (char *) "Generating staging directory ...");

		{		// Clean out the (new) staging/install directories
			char *cmd = NULL;
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
		for(dIt = this->depends.begin(); dIt != dEnds; dIt++) {
			if(!(*dIt)->extract_staging(staging_dir, done))
				return false;
		}
		log(this, (char *) "Done (%d)", done->size());
		delete done;
		free(staging_dir);
		staging_dir = NULL;


		if(this->depsExtraction != NULL) {
			// Extract installed files to a given location
			log(this, (char *) "Removing old install files ...");
			{
				std::unique_ptr < PackageCmd >
				    pc(new PackageCmd(pwd, "rm"));
				pc->addArg("-fr");
				pc->addArg(this->depsExtraction);

				if(!pc->Run(this)) {
					log(this,
					    (char *) "Failed to remove %s (pre-install)",
					    this->depsExtraction);
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

			log(this,
			    (char *) "Extracting installed files from dependencies ...");
			done = new std::list < std::string > ();
			for(dIt = this->depends.begin(); dIt != dEnds; dIt++) {
				if(!(*dIt)->extract_install(this->depsExtraction, done))
					return false;
			}
			delete done;
			log(this, (char *) "Dependency install files extracted");
		}

		std::list < PackageCmd * >::iterator cIt = this->commands.begin();
		std::list < PackageCmd * >::iterator cEnd = this->commands.end();

		log(this, (char *) "Running Commands");
		for(; cIt != cEnd; cIt++) {
			if(!(*cIt)->Run(this))
				return false;
		}
		log(this, (char *) "Done Commands");
	}

	log(this, (char *) "BUILT");

	if(this->bd != NULL) {
		std::unique_ptr < PackageCmd >
		    pc(new PackageCmd(this->bd->getNewStaging(), "pax"));
		pc->addArg("-x");
		pc->addArg("cpio");
		pc->addArg("-wf");
		pc->addArgFmt("%s/output/%s/staging/%s.tar.bz2", pwd,
			      this->getNS()->getName().c_str(), this->name.c_str());
		pc->addArg(".");

		if(!pc->Run(this)) {
			log(this, (char *) "Failed to compress staging directory");
			return false;
		}
	}

	if(this->bd != NULL) {
		if(this->installFile != NULL) {
			std::cout << "Copying " << std::string(this->installFile) <<
			    " to install folder\n";
			std::unique_ptr < PackageCmd >
			    pc(new PackageCmd(this->bd->getNewInstall(), "cp"));
			pc->addArg(this->installFile);
			pc->addArgFmt("%s/output/%s/install/%s", pwd,
				      this->getNS()->getName().c_str(), this->installFile);

			if(!pc->Run(this)) {
				log(this, (char *) "Failed to copy install file (%s) ",
				    this->installFile);
				return false;
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
				log(this, (char *) "Failed to compress install directory");
				return false;
			}
		}
	}

	if(this->bd != NULL) {
		char *cmd = NULL;
		// mv the build info file into the regular place
		asprintf(&cmd, "mv %s/.build.info.new %s/.build.info", this->bd->getPath(),
			 this->bd->getPath());
		system(cmd);
		free(cmd);
		cmd = NULL;

		if(this->isHashingOutput()) {
			// Hash the entire new path
			asprintf(&cmd,
				 "cd %s; find -type f -exec sha256sum {} \\; > %s/.output.info",
				 this->bd->getNewPath(), this->bd->getPath());
			system(cmd);
			free(cmd);
		}
	}

	free(pwd);

	clock_gettime(CLOCK_REALTIME, &end);

	this->run_secs = (end.tv_sec - start.tv_sec);

	log(this, (char *) "Built in %d seconds", this->run_secs);

#ifdef UNDERSCORE
	// lock
	us_mutex_lock(this->lock);
#endif
	this->building = false;
	this->built = true;
	this->was_built = true;
#ifdef UNDERSCORE
	// unlock
	us_mutex_unlock(this->lock);
#endif

	WORLD->packageFinished(this);

	return true;
}
