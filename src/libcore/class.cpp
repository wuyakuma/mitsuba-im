#include <mitsuba/mitsuba.h>

MTS_NAMESPACE_BEGIN

typedef std::map<std::string, Class *, std::less<std::string> > ClassMap;
static ClassMap *__classes;
static bool __isInitialized = false;
typedef Object *(*inst_function)();
typedef Object *(*unSer_function)(Stream *, InstanceManager *);

Class::Class(const std::string &name, bool abstract, const std::string &superClassName, void *instPtr, void *unSerPtr)
 : m_name(name), m_abstract(abstract), m_superClass(NULL), m_superClassName(superClassName), m_instPtr(instPtr), m_unSerPtr(unSerPtr) {
	if (__classes == NULL)
		__classes = new ClassMap();

	(*__classes)[name] = this;
}

const Class *Class::forName(const char *name) {
	if (name != NULL) {
		if (__classes->find(name) != __classes->end())
			return (*__classes)[name];
	}

	return NULL;
}

const Class *Class::forName(const std::string &name) {
	if (__classes->find(name) != __classes->end())
		return (*__classes)[name];

	return NULL;
}

bool Class::derivesFrom(const Class *theClass) const {
	const Class *mClass = this;

	while (mClass != NULL) {
		if (theClass == mClass)
			return true;
		mClass = mClass->getSuperClass();
	}

	return false;
}

void Class::initializeOnce(Class *theClass) {
	const std::string &base = theClass->m_superClassName;

	if (base != "") {
		if (__classes->find(base) != __classes->end()) {
			theClass->m_superClass = (*__classes)[base];
		} else {
			std::cerr << "Critical error during the static RTTI initialization: " << std::endl
				<< "Could not locate the base class '" << base << "' while initializing '"
				<< theClass->getName() << "'!" << std::endl;
			exit(-1);
		}
	}
}

void Class::staticInitialization() {
	std::for_each(__classes->begin(), __classes->end(), 
		compose1(std::ptr_fun(initializeOnce),
		select2nd<ClassMap::value_type>()));
	__isInitialized = true;
}

Object *Class::instantiate() const {
	if (m_instPtr == NULL) {
		SLog(EError, "RTTI error: An attempt to instantiate a "
			"class lacking the instantiation feature occurred (%s)!",
			getName().c_str());
	}
	return ((inst_function) m_instPtr)();
}

Object *Class::unserialize(Stream *stream, InstanceManager *manager) const {
	if (m_unSerPtr == NULL) {
		SLog(EError, "RTTI error: An attempt to instantiate a "
			"class lacking the unserialization feature occurred (%s)!",
			getName().c_str());
	}
	return ((unSer_function) m_unSerPtr)(stream, manager);
}

void Class::staticShutdown() {
	for (ClassMap::iterator it = __classes->begin(); it!= __classes->end(); ++it)
		delete (*it).second;
	delete __classes;
	__classes = NULL;
	__isInitialized = false;
}

MTS_NAMESPACE_END