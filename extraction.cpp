#include <buildsys.h>

static char * hash_file(const char *fname)
{
	char *cmd = NULL;
	asprintf(&cmd, "sha256sum %s", fname);
	FILE *f = popen(cmd,"r");
	free(cmd);
	char *Hash = (char *)calloc(65, sizeof(char));
	fread(Hash, sizeof(char), 64, f);
	fclose(f);
	return Hash;	
}

bool Extraction::add(ExtractionUnit *eu)
{
	ExtractionUnit **t = this->EUs;
	this->EU_count++;
	this->EUs = (ExtractionUnit **)realloc(t, sizeof(ExtractionUnit *) * this->EU_count);
	if(this->EUs == NULL)
	{
		this->EUs = t;
		this->EU_count--;
		return false;
	}
	this->EUs[this->EU_count-1] = eu;
	return true;
}

TarExtractionUnit::TarExtractionUnit(const char *fname)
{
	this->uri = std::string(fname);
	char *Hash = hash_file(fname);
	this->hash = std::string(Hash);
	free(Hash);
}

bool TarExtractionUnit::extract(Package *P, BuildDir *bd)
{
	char **argv = NULL;
	
	int res = mkdir("dl", 0700);
	if((res < 0) && (errno != EEXIST))
	{
		throw CustomException("Error: Creating download directory");
	}
	argv = (char **)calloc(4, sizeof(char *));
	argv[0] = strdup("tar");
	argv[1] = strdup("xf");
	argv[2] = strdup(this->uri.c_str());
	
	if(run(P->getName().c_str(), (char *)"tar", argv , bd->getPath(), NULL) != 0)
		throw CustomException("Failed to extract file");
	
	if(argv != NULL)
	{
		int i = 0;
		while(argv[i] != NULL)
		{
			free(argv[i]);
			i++;
		}
		free(argv);
	}
	return true;
}

PatchExtractionUnit::PatchExtractionUnit(int level, char *pp, char *uri)
{
	this->uri = std::string(uri);
	char *Hash = hash_file(uri);
	this->hash = std::string(Hash);
	free(Hash);
	this->level = level;
	this->patch_path = strdup(pp);
}

bool PatchExtractionUnit::extract(Package *P, BuildDir *bd)
{
	char **argv = (char **)calloc(7, sizeof(char *));
	argv[0] = strdup("patch");
	asprintf(&argv[1], "-p%i", this->level);
	argv[2] = strdup("-stN");
	argv[3] = strdup("-i");
	
	argv[4] = strdup(this->uri.c_str());
	argv[5] = strdup("--dry-run");
	if(run(P->getName().c_str(), (char *)"patch", argv , patch_path, NULL) != 0)
	{
		log(P->getName().c_str(), "Patch file: %s", argv[4]);
		throw CustomException("Will fail to patch");
	}
	free(argv[5]);
	argv[5] = NULL;
	if(run(P->getName().c_str(), (char *)"patch", argv , patch_path, NULL) != 0)
		throw CustomException("Truely failed to patch");

	if(argv != NULL)
	{
		int i = 0;
		while(argv[i] != NULL)
		{
			free(argv[i]);
			i++;
		}
		free(argv);
	}
	return true;
}