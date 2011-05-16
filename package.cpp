#include <buildsys.h>

std::list<Package *>::iterator Package::dependsStart()
{
	return this->depends.begin();
}
std::list<Package *>::iterator Package::dependsEnd()
{
	return this->depends.end();
}

BuildDir *Package::builddir()
{
	if(this->bd == NULL)
	{
		this->bd = new BuildDir(this->name);
	}
	
	return this->bd;
}

void Package::printLabel(std::ostream& out)
{
	out << "[label=\"";
	
	out << this->getName() << "\\n";
	out << "Cmds:" << this->commands.size();
	
	out << "\"]";
	
}

bool Package::process()
{
	if(this->visiting == true)
	{
		std::cout << this->name << ": dependency loop!!!" << std::endl;
		return false;
	}
	if(this->processed == true) return true;
	
	this->visiting = true;

	std::cout << "Processing: " << this->name << " (" << this->file << ")" << std::endl;

	WORLD->getLua()->setGlobal(std::string("P"), this);

	WORLD->getLua()->processFile(file.c_str());

	std::cout << "Done: " << this->name << std::endl;

	this->processed = true;

	std::list<Package *>::iterator iter = this->depends.begin();
	std::list<Package *>::iterator end = this->depends.end();

	for(; iter != end; iter++)
	{
		if(!(*iter)->process())
		{
			std::cout << this->name << ": dependency failed" << std::endl;
			return false;
		}
	}

	this->visiting = false;
	return true;
}
