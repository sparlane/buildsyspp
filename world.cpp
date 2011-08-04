#include <buildsys.h>

std::list<Package *>::iterator World::packagesStart()
{
	return this->packages.begin();
}
std::list<Package *>::iterator World::packagesEnd()
{
	return this->packages.end();
}

void World::setName(std::string n)
{
	this->name = n;

#ifdef UNDERSCORE
	sendTarget(this->name.c_str());
#endif
}

void World::setFeature(std::string key, std::string value, bool override)
{
	// look for this key
	if(features->find(key) != features->end())
	{
		if(override)
		{
			// only over-write the value if we are explicitly told to
			(*features)[key] = value;
		}
		return;
	}
	features->insert(std::pair<std::string, std::string>(key, value));
}

void World::setFeature(char *kv)
{
	char *eq = strchr(kv, '=');
	if(eq == NULL) throw CustomException("Features must be described as feature=value\n");
	char *temp = (char *)calloc(1, (eq - kv) + 1);
	if(temp == NULL) throw MemoryException();
	strncpy(temp, kv, (eq-kv));
	std::string key(temp);
	free(temp);
	eq++;
	temp = strdup(eq);
	if(temp == NULL) throw MemoryException();
	std::string value(temp);
	free(temp);
	this->setFeature(key, value, true);
}


std::string World::getFeature(std::string key)
{
	if(features->find(key) != features->end())
	{
		return (*features)[key];
	}
	throw NoKeyException();
}

#ifdef UNDERSCORE
static us_condition *t_cond = NULL;

static void *build_thread(us_thread *t)
{
	Package *p = (Package *)t->priv;
	
	log(p->getName().c_str(), "Build Thread");

	bool skip = false;

	if(p->isBuilding())
	{
		skip = true;
	}
	if(!skip)
		p->setBuilding();
	us_cond_lock(t_cond);
	us_cond_signal(t_cond, true);
	us_cond_unlock(t_cond);
	if(!skip)
	{
		try {
			p->build();
		} catch(Exception &E)
		{
			error(E.error_msg());
		}
	}
	WORLD->condTrigger();
	return NULL;
}
#endif

bool World::basePackage(char *filename)
{
	this->p = findPackage(filename, filename);

	this->p->process();
	
	this->graph = new Internal_Graph();
	this->topo_graph = new Internal_Graph();

	this->topo_graph->topological();
#ifdef UNDERSCORE
	t_cond = us_cond_create();
	while(!this->p->isBuilt())
	{
		us_cond_lock(this->cond);
		Package *toBuild = this->topo_graph->topoNext();
		if(toBuild != NULL)
		{
			us_cond_unlock(this->cond);
			us_cond_lock(t_cond);
			us_thread_create(build_thread, 0, toBuild);
			us_cond_wait(t_cond);
			us_cond_unlock(t_cond);
		} else {
			us_cond_wait(this->cond);
			us_cond_unlock(this->cond);
		}
	}
#else
	this->p->build();
#endif

	return true;
}

Package *World::findPackage(std::string name, std::string file)
{
	std::list<Package *>::iterator iter = this->packagesStart();
	std::list<Package *>::iterator iterEnd = this->packagesEnd();
	for(; iter != iterEnd; iter++)
	{
		if((*iter)->getName().compare(name) == 0)
			return (*iter);
	}
	Package *p = new Package(name, file);
	this->packages.push_back(p);
	return p;
}

bool World::isForced(std::string name)
{
	string_list::iterator fIt = this->forcedDeps->begin();
	string_list::iterator fEnd = this->forcedDeps->end();
	
	for(; fIt != fEnd; fIt++)
	{
		if((*fIt).compare(name)==0) return true;
	}
	return false;
}

bool World::packageFinished(Package *p)
{
#ifdef UNDERSCORE
	us_cond_lock(this->cond);
#endif
	this->topo_graph->deleteNode(p);
	this->topo_graph->topological();
#ifdef UNDERSCORE
	us_cond_signal(this->cond, true);
	us_cond_unlock(this->cond);
#endif
	return true;
}
