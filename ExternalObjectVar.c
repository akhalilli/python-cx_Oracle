//-----------------------------------------------------------------------------
// ExternalObjectVar.c
//   Defines the routines for handling object variables external to this
// module.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// external Object type
//-----------------------------------------------------------------------------
typedef struct {
    PyObject_HEAD
    PyObject *referencedObject;
    udt_ObjectType *objectType;
    dvoid *instance;
    dvoid *indicator;
    int isIndependent;
} udt_ExternalObjectVar;


//-----------------------------------------------------------------------------
// Declaration of external object variable functions.
//-----------------------------------------------------------------------------
static void ExternalObjectVar_Free(udt_ExternalObjectVar*);
static PyObject *ExternalObjectVar_GetAttr(udt_ExternalObjectVar*, PyObject*);
static PyObject *ExternalObjectVar_ConvertToPython(udt_Environment*,
        OCITypeCode, dvoid*, dvoid*, PyObject*, udt_ObjectType*);


//-----------------------------------------------------------------------------
// Python type declaration
//-----------------------------------------------------------------------------
static PyTypeObject g_ExternalObjectVarType = {
    PyObject_HEAD_INIT(NULL)
    0,					// ob_size
    "cx_Oracle.OBJECT",			// tp_name
    sizeof(udt_ExternalObjectVar),	// tp_basicsize
    0,					// tp_itemsize
    (destructor) ExternalObjectVar_Free,
                                        // tp_dealloc
    0,					// tp_print
    0,					// tp_getattr
    0,					// tp_setattr
    0,					// tp_compare
    0,					// tp_repr
    0,					// tp_as_number
    0,					// tp_as_sequence
    0,					// tp_as_mapping
    0,					// tp_hash
    0,					// tp_call
    0,					// tp_str
    (getattrofunc) ExternalObjectVar_GetAttr,
                                        // tp_getattro
    0,					// tp_setattro
    0,					// tp_as_buffer
    Py_TPFLAGS_DEFAULT,			// tp_flags
    0					// tp_doc
};


//-----------------------------------------------------------------------------
// ExternalObjectVar_New()
//   Create a new external LOB variable.
//-----------------------------------------------------------------------------
PyObject *ExternalObjectVar_New(
    PyObject *referencedObject,		// referenced object
    udt_ObjectType *objectType,		// type of object
    dvoid *instance,			// object instance data
    dvoid *indicator,			// indicator structure
    int isIndependent)			// is object independent?
{
    udt_ExternalObjectVar *newVar;

    newVar = PyObject_NEW(udt_ExternalObjectVar, &g_ExternalObjectVarType);
    if (!newVar)
        return NULL;
    Py_INCREF(referencedObject);
    newVar->referencedObject = referencedObject;
    Py_INCREF(objectType);
    newVar->objectType = objectType;
    newVar->instance = instance;
    newVar->indicator = indicator;
    newVar->isIndependent = isIndependent;
    return (PyObject*) newVar;
}


//-----------------------------------------------------------------------------
// ExternalObjectVar_Free()
//   Free an external LOB variable.
//-----------------------------------------------------------------------------
static void ExternalObjectVar_Free(
    udt_ExternalObjectVar *self)	// variable to free
{
    if (self->isIndependent)
        OCIObjectFree(self->objectType->environment->handle,
                self->objectType->environment->errorHandle,
                self->instance, OCI_OBJECTFREE_FORCE);
    Py_DECREF(self->objectType);
    Py_DECREF(self->referencedObject);
    PyObject_DEL(self);
}


//-----------------------------------------------------------------------------
// ExternalObjectVar_ConvertCollectionElements()
//   Convert the collection elements to Python values.
//-----------------------------------------------------------------------------
static int ExternalObjectVar_ConvertCollectionElements(
    udt_Environment *environment,	// environment to use
    OCIIter *iter,			// iterator
    PyObject *list,			// list result
    PyObject *referencedObject,		// referenced object
    udt_ObjectType *objectType)		// collection type information
{
    dvoid *elementValue, *elementIndicator;
    PyObject *elementObject;
    boolean endOfCollection;
    sword status;

    while (list) {
        status = OCIIterNext(environment->handle, environment->errorHandle,
                iter, &elementValue, &elementIndicator, &endOfCollection);
        if (Environment_CheckForError(environment, status,
                "ExternalObjectVar_ConvertCollection(): get next") < 0)
            return -1;
        if (endOfCollection)
            break;
        elementObject = ExternalObjectVar_ConvertToPython(environment,
                objectType->elementTypeCode, elementValue, elementIndicator,
                referencedObject, (udt_ObjectType*) objectType->elementType);
        if (!elementObject)
            return -1;
        if (PyList_Append(list, elementObject) < 0) {
            Py_DECREF(elementObject);
            return -1;
        }
        Py_DECREF(elementObject);
    }

    return 0;
}


//-----------------------------------------------------------------------------
// ExternalObjectVar_ConvertCollection()
//   Convert a collection to a Python list.
//-----------------------------------------------------------------------------
static PyObject *ExternalObjectVar_ConvertCollection(
    udt_Environment *environment,	// environment to use
    OCIColl *collectionValue,		// collection value
    PyObject *referencedObject,		// referenced object
    udt_ObjectType *objectType)		// collection type information
{
    PyObject *list;
    OCIIter *iter;
    sword status;
    int result;

    // create the iterator
    status = OCIIterCreate(environment->handle, environment->errorHandle,
            collectionValue, &iter);
    if (Environment_CheckForError(environment, status,
            "ExternalObjectVar_ConvertCollection(): creating iterator") < 0)
        return NULL;

    // create the result list
    list = PyList_New(0);
    if (list) {
        result = ExternalObjectVar_ConvertCollectionElements(environment, iter,
                list, referencedObject, objectType);
        if (result < 0) {
            Py_DECREF(list);
            list = NULL;
        }
    }
    OCIIterDelete(environment->handle, environment->errorHandle, &iter);

    return list;
}


//-----------------------------------------------------------------------------
// ExternalObjectVar_ConvertToPython()
//   Convert an Oracle value to a Python value.
//-----------------------------------------------------------------------------
static PyObject *ExternalObjectVar_ConvertToPython(
    udt_Environment *environment,	// environment to use
    OCITypeCode typeCode,		// type of Oracle data
    dvoid *value,			// Oracle value
    dvoid *indicator,			// null indicator
    PyObject *referencedObject,		// referenced object (for sub objects)
    udt_ObjectType *subType)		// sub type (for sub objects)
{
    OraText *stringValue;

    // null values returned as None
    if (* (OCIInd*) indicator == OCI_IND_NULL) {
        Py_INCREF(Py_None);
        return Py_None;
    }

    switch (typeCode) {
        case OCI_TYPECODE_VARCHAR2:
            stringValue = OCIStringPtr(environment->handle,
                    * (OCIString**) value);
            return PyString_FromString((char*) stringValue);
        case OCI_TYPECODE_NUMBER:
            return OracleNumberToPythonFloat(environment, (OCINumber*) value);
        case OCI_TYPECODE_DATE:
            return OracleDateToPythonDate(&vt_DateTime, (OCIDate*) value);
        case OCI_TYPECODE_OBJECT:
            return ExternalObjectVar_New(referencedObject, subType, value,
                    indicator, 0);
        case OCI_TYPECODE_NAMEDCOLLECTION:
            return ExternalObjectVar_ConvertCollection(environment,
                    * (OCIColl**) value, referencedObject, subType);
    };

    return PyErr_Format(g_NotSupportedErrorException,
            "ExternalObjectVar_GetAttributeValue(): unhandled data type %d",
            typeCode);
}


//-----------------------------------------------------------------------------
// ExternalObjectVar_GetAttributeValue()
//   Retrieve an attribute on the external LOB variable object.
//-----------------------------------------------------------------------------
static PyObject *ExternalObjectVar_GetAttributeValue(
    udt_ExternalObjectVar *self,	// object
    udt_ObjectAttribute *attribute)	// attribute to get
{
    dvoid *valueIndicator, *value;
    OCIInd scalarValueIndicator;
    ub4 nameLength;
    sword status;
    OCIType *tdo;
    char *name;

    // get the value for the attribute
    name = PyString_AS_STRING(attribute->name);
    nameLength = PyString_GET_SIZE(attribute->name);
    status = OCIObjectGetAttr(self->objectType->environment->handle,
            self->objectType->environment->errorHandle, self->instance,
            self->indicator, self->objectType->tdo, (const OraText**) &name,
            &nameLength, 1, 0, 0, &scalarValueIndicator, &valueIndicator,
            &value, &tdo);
    if (Environment_CheckForError(self->objectType->environment, status,
            "ExternalObjectVar_GetAttributeValue(): getting value") < 0)
        return NULL;

    // determine the proper null indicator
    if (!valueIndicator)
        valueIndicator = &scalarValueIndicator;

    return ExternalObjectVar_ConvertToPython(self->objectType->environment,
            attribute->typeCode, value, valueIndicator, (PyObject*) self,
            attribute->subType);
}


//-----------------------------------------------------------------------------
// ExternalObjectVar_GetAttr()
//   Retrieve an attribute on the external LOB variable object.
//-----------------------------------------------------------------------------
static PyObject *ExternalObjectVar_GetAttr(
    udt_ExternalObjectVar *self,	// object
    PyObject *nameObject)		// name of attribute
{
    udt_ObjectAttribute *attribute;
    char *name;

    attribute = (udt_ObjectAttribute*)
            PyDict_GetItem(self->objectType->attributesByName, nameObject);
    if (attribute)
        return ExternalObjectVar_GetAttributeValue(self, attribute);
    name = PyString_AS_STRING(nameObject);
    if (name[0] == 't' && strcmp(name, "type") == 0) {
        Py_INCREF(self->objectType);
        return (PyObject*) self->objectType;
    }

    PyErr_SetString(PyExc_AttributeError, name);
    return NULL;
}
