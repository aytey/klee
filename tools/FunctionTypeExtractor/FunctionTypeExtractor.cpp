#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Tooling/ArgumentsAdjusters.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "json-c/json.h"

bool isFirstTraverse = false;
bool isSecondTraverse = false;

std::set<std::string> functionNameList; // Record all function names
std::set<std::string> structNameList; // Record all to-be-processed structure names
std::set<std::string> processedStructNameList; // Record all processed structure names
std::map<std::string, std::string> typedefMap; // Record typedef info: type before and after typedef
std::map<std::string, std::string> typedefSrcInfoMap; // Record typedef info: source info and name after typedef
std::set<std::string> usedTypedefTypeList; // Record all used "before typedef types"
std::set<std::string> globalVarNameList; // Record all global variable names

struct json_object* json_file = json_object_new_object();
struct json_object* info_array = json_object_new_array();

using namespace clang;
using namespace llvm;

// Help message
static cl::extrahelp CommonHelp(tooling::CommonOptionsParser::HelpMessage);
static cl::extrahelp MoreHelp(
        "\tFor example, to run clang-stat on all files in a subtree of the\n"
        "\tsource tree, use:\n"
        "\n"
        "\t  find path/in/subtree -name '*.cpp'|xargs clang-stat\n"
        "\n"
        "\tor using a specific build path:\n"
        "\n"
        "\t  find path/in/subtree -name '*.cpp'|xargs clang-stat -p build/path\n"
        "\n"
        "\tNote, that path/in/subtree and current directory should follow the\n"
        "\trules described above.\n"
        "\n"
);

static cl::OptionCategory ClangStatCategory("clang-stat options");

// Record type related information
typedef struct typeInfo {
    std::string typeKind; // "BuiltinType", "StructureType" and so on
    int64_t dimension; // If it is a pointer type, record dimension
    QualType pointeeType; // If it is a pointer type, record pointee type
} typeInfo;

// Record array type related information
typedef struct arrayTypeInfo {
    QualType elementType; // Element type
    int64_t elementSize; // Element size
    int64_t arrayLength; // Array length
} arrayTypeInfo;

// Record function parameter related information
typedef struct paramInfo {
    QualType paramType;
    std::string paramName;
    typeInfo pTI;
} paramInfo;

// Record function pointer type related information
typedef struct functionPointerTypeInfo {
    QualType returnType; // Return type of the function
    int64_t returnTypeSize; // Return type size of the function
    typeInfo rTI; // Return type related information
    std::vector<paramInfo> paramInfoList;
} functionPointerTypeInfo;

std::map<std::string, functionPointerTypeInfo> functionPointerMap;


class ClangStatASTVisitor : public RecursiveASTVisitor<ClangStatASTVisitor> {
public:
    explicit ClangStatASTVisitor(ASTContext *Context)
        : Context(Context) {}

    // Get kind, (dimension and pointee type, if type if pointer type) of type
    typeInfo getTypeInfo(QualType type) {
        std::string typeKind;
        int64_t dimension = 0;
        auto pointeeType = type;
        if(type->isBuiltinType()) {
            typeKind = "BuiltinType";
        } else if(type->isArrayType()) {
            typeKind = "ArrayType";
        } else if(type->isStructureType() || type->isUnionType()) {
            typeKind = "StructType";
        } else if(type->isAnyPointerType()) {
            // If type is pointer type, get dimension and pointee type
            while(pointeeType->isAnyPointerType()) {
                dimension += 1;
                pointeeType = pointeeType->getPointeeType();
            }
            if(pointeeType->isBuiltinType()) {
                typeKind = "BuiltinPointerType";
            } else if(pointeeType->isStructureType() || pointeeType->isUnionType()) {
                typeKind = "StructPointerType";
            } else if(pointeeType->isFunctionProtoType()) {
                typeKind = "FunctionPointerType";
            }
        }

        typeInfo tI;
        tI.typeKind = typeKind;
        tI.dimension = dimension;
        tI.pointeeType = pointeeType;

        return tI;
    }

    // Check whether the type is a "after typedef type"
    // If so, add the corresponding "before typedef type" to usedTypedefTypeList
    // For example, if there exists a typedef declaration "typedef int cJSON_bool"
    // and a function "f" has a return type "cJSON_bool", we add "int" to usedTypedefTypeList
    void checkUsedTypedefType(std::string typeString) {
        for(auto & it : typedefMap) {
            if(it.second == typeString) {
                usedTypedefTypeList.insert(it.first);
                break;
            }
        }
    }

    // Handle array type
    arrayTypeInfo handleArrayType(VarDecl *v) {
        // Get element type, element size and array length
        auto elementType = cast<clang::ArrayType>(v->getType())->getElementType();
        auto elementSizeInfo = v->getASTContext().getTypeInfo(elementType);
        auto elementSize = (int64_t)(elementSizeInfo.Width / 8);
        auto varSizeInfo = v->getASTContext().getTypeInfo(v->getType());
        auto varSize = (int64_t)(varSizeInfo.Width / 8);
        auto arrayLength = (int64_t)(varSize / elementSize);

        arrayTypeInfo aTI;
        aTI.elementType = elementType;
        aTI.elementSize = elementSize;
        aTI.arrayLength = arrayLength;

        return aTI;
    }

    // Handle structure type
    void handleStructType(QualType type) {
        std::string structName = type.getAsString();
        // If structure is decorated with keyword "const", remove "const";
        if(type.isConstant(*Context)) {
            structName = structName.substr(6, structName.size()-6);
        }
        // If the structure is not processed, add it to the to-be-processed structure list
        if(processedStructNameList.find(structName) == processedStructNameList.end()) {
            structNameList.insert(structName);
        }
    }

    // Handle function pointer type
    functionPointerTypeInfo handleFunctionPointerType(QualType pointeeType) {
        // Get return type and return type size
        auto returnType = pointeeType->castAs<FunctionProtoType>()->getReturnType();
        auto returnTypeSizeInfo = Context->getTypeInfo(returnType);
        auto returnTypeSize = (int64_t)(returnTypeSizeInfo.Width / 8);

        // Get kind, (dimension and pointee type, if type if pointer type) of return type
        typeInfo rTI = getTypeInfo(returnType);

        functionPointerTypeInfo fPTI;
        fPTI.returnType = returnType;
        fPTI.returnTypeSize = returnTypeSize;
        fPTI.rTI = rTI;

        uint64_t paramNums = pointeeType->castAs<FunctionProtoType>()->getNumParams();
        for (uint64_t i = 0; i < paramNums; i++) {
            paramInfo pI;
            pI.paramType = pointeeType->castAs<FunctionProtoType>()->getParamType(i);
            pI.paramName = "";
            pI.pTI = getTypeInfo(pI.paramType);
            fPTI.paramInfoList.push_back(pI);
        }

        return fPTI;
    }

  // Get function information
    bool VisitFunctionDecl(FunctionDecl *f) {
        // Only get function declaration in the second traverse
        if(!isSecondTraverse)
            return true;

        // Determine whether the function is in the main file
        // If the function is not in the current file, do not handle
        if(!Context->getSourceManager().isInMainFile(f->getLocation()))
            return true;

        // Determine whether it is a function definition
        // If it is a declaration and not a definition, do not handle
        if(!f->hasBody())
            return true;

        // Get function name
        auto functionName = f->getName();
        functionNameList.insert(functionName.str());

        // Get return type of function
        auto functionReturnType = f->getReturnType();
        auto functionReturnTypeSize = (int64_t)(functionReturnType.Width / 8);

        // Check whether the function return type is a "after typedef type"
        checkUsedTypedefType(functionReturnType.getAsString());

        // Get begin and end line number of the function
        SourceManager &SrcMgr = f->getASTContext().getSourceManager();
        SourceLocation BeginLoc = SrcMgr.getFileLoc(f->getLocStart());
        SourceLocation EndLoc = SrcMgr.getFileLoc(f->getLocEnd());
        auto beginDecomposedLoc = SrcMgr.getDecomposedLoc(BeginLoc);
        auto endDecomposedLoc = SrcMgr.getDecomposedLoc(EndLoc);
        auto beginLineNumber = (int64_t)(SrcMgr.getLineNumber(beginDecomposedLoc.first, beginDecomposedLoc.second));
        auto endLineNumber = (int64_t)(SrcMgr.getLineNumber(endDecomposedLoc.first, endDecomposedLoc.second));
//        const char *beginChar = SrcMgr.getCharacterData(BeginLoc);
//        const char *endChar = SrcMgr.getCharacterData(EndLoc);
//        std::string sourceCode = std::string(beginChar, endChar - beginChar + 1);

        struct json_object* info = json_object_new_object();
        json_object_object_add(info, "InfoType", json_object_new_string("FunctionDecl"));
        json_object_object_add(info, "FunctionName", json_object_new_string(functionName.str().c_str()));
        json_object_object_add(info, "ReturnType", json_object_new_string(functionReturnType.getAsString().c_str()));
        json_object_object_add(info, "BeginLine", json_object_new_int64(beginLineNumber));
        json_object_object_add(info, "EndLine", json_object_new_int64(endLineNumber));
//        json_object_object_add(info, "SourceCode", json_object_new_string(sourceCode.c_str()));

        // Get information of function parameters
        struct json_object* param_array = json_object_new_array();
        unsigned paramNum = f->getNumParams();
        for(unsigned i = 0; i < paramNum; i++) {
            // Get parameter type, name, size and type kind
            ParmVarDecl *param = f->getParamDecl(i);
            auto paramType = param->getType();
            auto paramName = param->getName();
            auto paramSizeInfo = param->getASTContext().getTypeInfo(paramType);
            auto paramSize = (int64_t)(paramSizeInfo.Width / 8);

            // Get parameter type related information
            typeInfo tI = getTypeInfo(paramType);
            std::string paramTypeKind = tI.typeKind;
            uint64_t dimension = tI.dimension;
            QualType pointeeType = tI.pointeeType;

            // Check whether the parameter type is a "after typedef type"
            checkUsedTypedefType(paramType.getAsString());

            struct json_object* param_info = json_object_new_object();
            json_object_object_add(param_info, "ParamType", json_object_new_string(paramType.getAsString().c_str()));
            json_object_object_add(param_info, "ParamName", json_object_new_string(paramName.str().c_str()));
            json_object_object_add(param_info, "ParamSize", json_object_new_int64(paramSize));
            json_object_object_add(param_info, "ParamTypeKind", json_object_new_string(paramTypeKind.c_str()));

            if(paramTypeKind == "StructType") {
                // Handle structure type
                // Add structure to to-be-processed structure list
                handleStructType(paramType);
            } else if(paramTypeKind == "BuiltinPointerType" || paramTypeKind == "StructPointerType") {
                // If parameter type is pointer type, add "dimension" and "pointee type" to parameter information
                json_object_object_add(param_info, "Dimension", json_object_new_int64(dimension));
                json_object_object_add(param_info, "PointeeType", json_object_new_string(pointeeType.getAsString().c_str()));
                if(paramTypeKind == "StructPointerType") {
                    // Handle pointee type of structure pointer
                    // Add structure to to-be-processed structure list
                    handleStructType(pointeeType);
                    checkUsedTypedefType(pointeeType.getAsString());
                }
            } else if(paramTypeKind == "FunctionPointerType") {
                // Get return type related information of function pointer
//                functionPointerTypeInfo fPTI = handleFunctionPointerType(pointeeType);
//                auto returnType = fPTI.returnType;
//                uint64_t returnTypeSize = fPTI.returnTypeSize;
//                std::string returnTypeKind = fPTI.rTI.typeKind;
//                dimension = fPTI.rTI.dimension;
//                pointeeType = fPTI.rTI.pointeeType;
//
//                struct json_object* return_type_info = json_object_new_object();
//                json_object_object_add(return_type_info, "ReturnType", json_object_new_string(returnType.getAsString().c_str()));
//                json_object_object_add(return_type_info, "ReturnTypeSize", json_object_new_int64(returnTypeSize));
//                json_object_object_add(return_type_info, "ReturnTypeKind", json_object_new_string(returnTypeKind.c_str()));
//
//                // If return type is a pointer type, add "dimension" and "pointee type" to return type information
//                if(returnTypeKind == "BuiltinPointerType") {
//                    json_object_object_add(return_type_info, "Dimension", json_object_new_int64(dimension));
//                    json_object_object_add(return_type_info, "PointeeType", json_object_new_string(pointeeType.getAsString().c_str()));
//                }
//                json_object_object_add(param_info, "ReturnTypeInfo", return_type_info);
//
//                std::vector<paramInfo> paramInfoList = fPTI.paramInfoList;
//                struct json_object* paramTypeInfoArray = json_object_new_array();
//                for(auto & paramInfo : paramInfoList) {
//                    struct json_object* paramTypeInfo = json_object_new_object();
//                    paramType = paramInfo.paramType;
//                    json_object_object_add(paramTypeInfo, "ParamType", json_object_new_string(paramType.getAsString().c_str()));
//                    json_object_array_add(paramTypeInfoArray, paramTypeInfo);
//                }
//                json_object_object_add(param_info, "ParamTypeInfo", paramTypeInfoArray);
            }
            json_object_array_add(param_array, param_info);
        }
        json_object_object_add(info, "ParamInfo", param_array);
        json_object_array_add(info_array, info);
        return true;
    }

    // Get typedef information
    bool VisitTypedefDecl(const TypedefDecl *t) {
        // Only get typedef declaration in the first traverse
        if(!isFirstTraverse)
            return true;

        // Get "before typedef type" and "after typedefe type"
        // And map "before typedef type" to "after typedef type"
        std::string typeBeforeTypedef = t->getUnderlyingType().getAsString();
        std::string typeAfterTypedef = t->getNameAsString();
        typedefMap[typeBeforeTypedef] =  typeAfterTypedef;

        // If the type is function pointer type, add a bool variable to identify it
        // Besides, record the return type and parameters information of the function pointer type variable
        auto type = t->getUnderlyingType();
        typeInfo tI = getTypeInfo(type);
        std::string typeKind = tI.typeKind;
        QualType pointeeType = tI.pointeeType;

        if(typeKind == "FunctionPointerType") {
            functionPointerTypeInfo fPTI = handleFunctionPointerType(pointeeType);
            functionPointerMap[typeAfterTypedef] = fPTI;
        }

        // If "before typedef type" is a structure or union type
        // Get the source information of the typedef declaration
        // Why do we need this?
        // When processing typedef declaration like this
        // typedef struct {
        //     const unsigned char *json;
        //     size_t position;
        // } error;
        // We get following information when handling typedef declaration
        // "before typedef type": struct error  "after typedef type": error
        // However, when handling record declaration
        // we can only know that the "before typedef type" is a anonymous struct type
        // So, we can not get its "after typedef type"
        // Here, we use source information to solve the problem
        // We map the source information to "after typedef type"
        // When processing record declaration, we can get the source information
        // hence, we can get the "after typedef type" from source information map
        // Further more, we can map the anonymous struct to "after typedef type"
        /// Is there a better way to solve it ?
        if(t->getUnderlyingType()->isStructureType() || t->getUnderlyingType()->isUnionType()) {
            SourceManager &SrcMgr = t->getASTContext().getSourceManager();
            const FileEntry *Entry = SrcMgr.getFileEntryForID(SrcMgr.getFileID(t->getLocation()));

            // Get file name of typedef declaration
            std::string fileName;
            if(Entry)
                fileName = Entry->getName().str();
            // Get line number of typedef declaration
            SourceLocation FileLoc = SrcMgr.getFileLoc(t->getLocStart());
            auto decomposedLoc = SrcMgr.getDecomposedLoc(FileLoc);
            unsigned lineNumber = SrcMgr.getLineNumber(decomposedLoc.first, decomposedLoc.second);

            // Map source information to "after typedef type"
            // "type" is used to tag it is a structure type or union type
            bool type = t->getUnderlyingType()->isUnionType();
            std::string srcInfo = itostr(type) + fileName + itostr(lineNumber);
            typedefSrcInfoMap[srcInfo] = t->getName().str();
        }

        return true;
    }

    // Get structure and union information
    bool VisitRecordDecl(const RecordDecl *r) {
        // Do not handle record declaration in the first and second traverse
        if(isFirstTraverse or isSecondTraverse)
            return true;

        // Determine whether it is a structure or union
        // If it is not, do not handle
        if(!r->isStruct() && !r->isUnion())
            return true;

        // Determine whether the field of structure or union is empty
        // If it is, do not handle
        if(r->field_empty()) {
            return true;
        }

        // Determine it is a structure or union
        std::string prefix;
        if(r->isStruct())
            prefix = "struct ";
        else
            prefix = "union ";

        std::string structName;
        // If its name is empty, it is an anonymous structure or union
        if(r->getName().str().empty()) {
            SourceManager &SrcMgr = r->getASTContext().getSourceManager();
            const FileEntry *Entry = SrcMgr.getFileEntryForID(SrcMgr.getFileID(r->getLocation()));
            // Get file name of the record declaration
            std::string fileName;
            if(Entry)
                fileName = Entry->getName().str();
            // Get line number of column number of the record declaration
            SourceLocation FileLoc = SrcMgr.getFileLoc(r->getLocStart());
            auto decomposedLoc = SrcMgr.getDecomposedLoc(FileLoc);
            unsigned lineNumber = SrcMgr.getLineNumber(decomposedLoc.first, decomposedLoc.second);
            unsigned columnNumber = SrcMgr.getColumnNumber(decomposedLoc.first, decomposedLoc.second);
            // Define anonymous structure or union with its file name, line number and column number
            structName = prefix + "(anonymous " + prefix + "at " + fileName + ":" + itostr(lineNumber) + ":" + itostr(columnNumber) + ")";

            // For handling such case
            // typedef struct {
            //     const unsigned char *json;
            //     size_t position;
            // } error;
            if(typedefSrcInfoMap.count(itostr(r->isUnion()) + fileName + itostr(lineNumber))) {
                typedefMap[structName] = typedefSrcInfoMap[itostr(r->isUnion()) + fileName + itostr(lineNumber)];
            }
        } else {
            structName = prefix + r->getName().str();
        }

        // If the structure or union has a "after typedef type", get it
        std::string typedefName;
        if(typedefMap.count(structName))
            typedefName = typedefMap[structName];


        // If the structure and "after typedef type" was processed, do not handle
        if(structNameList.find(structName) == structNameList.end() &&
            structNameList.find(typedefName) == structNameList.end())
            return true;

        // If the structure and "after typedef type" is not processed
        // Remove structure and "after typedef type" from to-be-processed structure list
        // And add structure and "after typedef type" to processed structure list
        structNameList.erase(structName);
        structNameList.erase(typedefName);
        processedStructNameList.insert(structName);
        if(!typedefName.empty()) {
            processedStructNameList.insert(typedefName);
            usedTypedefTypeList.insert(structName);
        }

        struct json_object* info = json_object_new_object();
        json_object_object_add(info, "InfoType", json_object_new_string("StructDecl"));
        json_object_object_add(info, "StructName", json_object_new_string(structName.c_str()));

        // Get field information of structure or union
        const ASTRecordLayout &recordLayout = r->getASTContext().getASTRecordLayout(r);
        struct json_object* field_array = json_object_new_array();
        unsigned i = 0;
        for(auto field = r->field_begin(); field != r->field_end(); ++field) {
            // Get field type, name, offset, size and type kind
            auto fieldType = field->getType();
            auto fieldName = field->getName();
            auto fieldOffset = (int64_t)(recordLayout.getFieldOffset(i) / 8);
            auto fieldSizeInfo = field->getASTContext().getTypeInfo(field->getType());
            auto fieldSize = (int64_t)(fieldSizeInfo.Width / 8);

            // Get field type related information
            typeInfo tI = getTypeInfo(fieldType);
            std::string fieldTypeKind = tI.typeKind;
            uint64_t dimension = tI.dimension;
            auto pointeeType = tI.pointeeType;

            struct json_object* field_info = json_object_new_object();
            json_object_object_add(field_info, "FieldType", json_object_new_string(fieldType.getAsString().c_str()));
            json_object_object_add(field_info, "FieldName", json_object_new_string(fieldName.str().c_str()));
            json_object_object_add(field_info, "FieldOffset", json_object_new_int64(fieldOffset));
            json_object_object_add(field_info, "FieldSize", json_object_new_int64(fieldSize));
            json_object_object_add(field_info, "FieldTypeKind", json_object_new_string(fieldTypeKind.c_str()));

            if(fieldTypeKind == "StructType") {
                // handle structure type
                // May add structure to to-be-processed structure list
                handleStructType(fieldType);
            } else if(fieldTypeKind == "BuiltinPointerType" || fieldTypeKind == "StructPointerType") {
                json_object_object_add(field_info, "Dimension", json_object_new_int64(dimension));
                json_object_object_add(field_info, "PointeeType", json_object_new_string(pointeeType.getAsString().c_str()));
                if(fieldTypeKind == "StructPointerType") {
                    // handle structure type
                    // May add structure to to-be-processed structure list
                    handleStructType(pointeeType);
                    checkUsedTypedefType(pointeeType.getAsString());
                }
            }
            json_object_array_add(field_array, field_info);
            i += 1;
        }
        json_object_object_add(info, "FieldInfo", field_array);
        json_object_array_add(info_array, info);
        return true;
    }

    // Get global variable information
    bool VisitVarDecl(VarDecl *v) {
        // Only get variable declaration in the second traverse
        if(!isSecondTraverse)
            return true;

        // Determine whether the variable is global
        // If not, do not handle
        if(!v->hasGlobalStorage())
            return true;

        // Determine whether the variable is in the main file
        // If not, do not handle
        if(!Context->getSourceManager().isInMainFile(v->getLocation()))
            return true;

        // Get global variable name
        auto varName = v->getName();

        // If the global variable is not processed
        // Add it to global variable list
        // Else, do not handle
        if(globalVarNameList.find(varName.str()) != globalVarNameList.end())
            return true;
        globalVarNameList.insert(varName.str());

        // Determine wheter the global variable is decorated with keyword "extern" or "static"
        // And get global variable type, size and type kind
        bool isExtern = v->getStorageClass() == StorageClass::SC_Extern;
        bool isStatic = v->getStorageClass() == StorageClass::SC_Static;
        auto varType = v->getType();
        auto varSizeInfo = v->getASTContext().getTypeInfo(v->getType());
        auto varSize = (int64_t)(varSizeInfo.Width / 8);

        // Get global variable type related information
        typeInfo tI = getTypeInfo(varType);
        std::string varTypeKind = tI.typeKind;
        uint64_t dimension = tI.dimension;
        auto pointeeType = tI.pointeeType;

        // Check whether the global variable type is a "after typedef type"
        checkUsedTypedefType(varType.getAsString());

        struct json_object* info = json_object_new_object();
        json_object_object_add(info, "InfoType", json_object_new_string("GlobalVarDecl"));
        json_object_object_add(info, "IsExtern", json_object_new_boolean(isExtern));
        json_object_object_add(info, "IsStatic", json_object_new_boolean(isStatic));
        json_object_object_add(info, "GlobalVarType", json_object_new_string(varType.getAsString().c_str()));
        json_object_object_add(info, "GlobalVarName", json_object_new_string(varName.str().c_str()));
        json_object_object_add(info, "GlobalVarSize", json_object_new_int64(varSize));
        json_object_object_add(info, "GlobalVarTypeKind", json_object_new_string(varTypeKind.c_str()));

        if(varTypeKind == "ArrayType") {
            // Handle array type
            // Get element type, size and array length
            arrayTypeInfo aTI = handleArrayType(v);
            auto elementType = aTI.elementType;
            uint64_t elementSize = aTI.elementSize;
            uint64_t arrayLength = aTI.arrayLength;
            json_object_object_add(info, "ElementType", json_object_new_string(elementType.getAsString().c_str()));
            json_object_object_add(info, "ElementSize", json_object_new_int64(elementSize));
            json_object_object_add(info, "ArrayLength", json_object_new_int64(arrayLength));
        } else if(varTypeKind == "StructType") {
            // Handle structure type
            // Add structure to to-be-processed structure list
            handleStructType(varType);
        } else if(varTypeKind == "BuiltinPointerType" || varTypeKind == "StructPointerType") {
            json_object_object_add(info, "Dimension", json_object_new_int64(dimension));
            json_object_object_add(info, "PointeeType", json_object_new_string(pointeeType.getAsString().c_str()));
            if(varTypeKind == "StructPointerType") {
                // Handle structure type
                // Add structure to to-be-processed structure list
                handleStructType(pointeeType);
                checkUsedTypedefType(pointeeType.getAsString());
            }
        } else if(varTypeKind == "FunctionPointerType") {
            // Get return type related information of function pointer
//            functionPointerTypeInfo fPTI = handleFunctionPointerType(pointeeType);
//            auto returnType = fPTI.returnType;
//            uint64_t returnTypeSize = fPTI.returnTypeSize;
//            std::string returnTypeKind = fPTI.rTI.typeKind;
//            dimension = fPTI.rTI.dimension;
//            pointeeType = fPTI.rTI.pointeeType;
//
//            struct json_object* return_type_info = json_object_new_object();
//            json_object_object_add(return_type_info, "ReturnType", json_object_new_string(returnType.getAsString().c_str()));
//            json_object_object_add(return_type_info, "ReturnTypeSize", json_object_new_int64(returnTypeSize));
//            json_object_object_add(return_type_info, "ReturnTypeKind", json_object_new_string(returnTypeKind.c_str()));
//
//            // If return type is a pointer type, add "dimension" and "pointee type" to return type information
//            if(returnTypeKind == "BuiltinPointerType") {
//                json_object_object_add(return_type_info, "Dimension", json_object_new_int64(dimension));
//                json_object_object_add(return_type_info, "PointeeType", json_object_new_string(pointeeType.getAsString().c_str()));
//            }
//            json_object_object_add(info, "ReturnTypeInfo", return_type_info);
//
//            std::vector<paramInfo> paramInfoList = fPTI.paramInfoList;
//            struct json_object* paramTypeInfoArray = json_object_new_array();
//            for(auto & paramInfo : paramInfoList) {
//                struct json_object* paramTypeInfo = json_object_new_object();
//                auto paramType = paramInfo.paramType;
//                json_object_object_add(paramTypeInfo, "ParamType", json_object_new_string(paramType.getAsString().c_str()));
//                json_object_array_add(paramTypeInfoArray, paramTypeInfo);
//            }
//            json_object_object_add(info, "ParamTypeInfo", paramTypeInfoArray);
        }

        json_object_array_add(info_array, info);
        return true;
    }

private:
    ASTContext *Context;

};

class ClangStatASTConsumer : public ASTConsumer {
public:
    explicit ClangStatASTConsumer(ASTContext *Context)
        : Visitor(Context) {}

    virtual void HandleTranslationUnit(ASTContext &Context) {
        // The first traverse is to collect typedef information
        isFirstTraverse = true;
        Visitor.TraverseDecl(Context.getTranslationUnitDecl());
        isFirstTraverse = false;
        // The second traverse is to collect function and global variable information
        isSecondTraverse = true;
        Visitor.TraverseDecl(Context.getTranslationUnitDecl());
        isSecondTraverse = false;
        // The other traverses is to collect structure information
        // Loop until all structures are processed
        std::set<std::string> before = structNameList;
        while(!structNameList.empty()) {
            Visitor.TraverseDecl(Context.getTranslationUnitDecl());
            // If we can not handle the structure of previous traverse in current traverse,
            // we abort handling the structure
            std::set<std::string> after = structNameList;
            for(const auto & it : after) {
                if(before.find(it) != before.end()) {
                    structNameList.erase(it);
                }
            }
            before = structNameList;
        }
        std::set<std::string> afterTypedefTypeList;
        for(const auto & it : usedTypedefTypeList){
            // typedef struct {
            //     const unsigned char *json;
            //     size_t position;
            // } error;
            // We have [struct anonymous.., error] and [struct error, error] in typedefMap
            // And (struct anonymous..., struct error) in usedTypedefTypeList
            // We only want [struct anonymous.., error], so we do this
            if(afterTypedefTypeList.find(typedefMap[it]) != afterTypedefTypeList.end() &&
                it == "struct " + typedefMap[it])
                continue;

            struct json_object* typedefInfo = json_object_new_object();
            json_object_object_add(typedefInfo, "InfoType", json_object_new_string("TypedefDecl"));
            json_object_object_add(typedefInfo, "TypeBefore", json_object_new_string(it.c_str()));
            json_object_object_add(typedefInfo, "TypeAfter", json_object_new_string(typedefMap[it].c_str()));
            if(functionPointerMap.count(typedefMap[it])) {
                json_object_object_add(typedefInfo, "IsFunctionPointerType", json_object_new_boolean(1));
                functionPointerTypeInfo fPTI = functionPointerMap[typedefMap[it]];
                auto returnType = fPTI.returnType;
                uint64_t returnTypeSize = fPTI.returnTypeSize;
                std::string returnTypeKind = fPTI.rTI.typeKind;
                uint64_t dimension = fPTI.rTI.dimension;
                auto pointeeType = fPTI.rTI.pointeeType;

                struct json_object* return_type_info = json_object_new_object();
                json_object_object_add(return_type_info, "ReturnType", json_object_new_string(returnType.getAsString().c_str()));
                json_object_object_add(return_type_info, "ReturnTypeSize", json_object_new_int64(returnTypeSize));
                json_object_object_add(return_type_info, "ReturnTypeKind", json_object_new_string(returnTypeKind.c_str()));

                // If return type is a pointer type, add "dimension" and "pointee type" to return type information
                if(returnTypeKind == "BuiltinPointerType") {
                    json_object_object_add(return_type_info, "Dimension", json_object_new_int64(dimension));
                    json_object_object_add(return_type_info, "PointeeType", json_object_new_string(pointeeType.getAsString().c_str()));
                }
                json_object_object_add(typedefInfo, "ReturnTypeInfo", return_type_info);

                std::vector<paramInfo> paramInfoList = fPTI.paramInfoList;
                struct json_object* paramTypeInfoArray = json_object_new_array();
                for(auto & paramInfo : paramInfoList) {
                    struct json_object* paramTypeInfo = json_object_new_object();
                    auto paramType = paramInfo.paramType;
                    json_object_object_add(paramTypeInfo, "ParamType", json_object_new_string(paramType.getAsString().c_str()));
                    json_object_array_add(paramTypeInfoArray, paramTypeInfo);
                }
                json_object_object_add(typedefInfo, "ParamTypeInfo", paramTypeInfoArray);
            }
            json_object_array_add(info_array, typedefInfo);
            afterTypedefTypeList.insert(typedefMap[it]);
        }
    }

private:
    ClangStatASTVisitor Visitor;
};

class ClangStatAction : public ASTFrontendAction {
public:
    virtual std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, llvm::StringRef InFile) {
        return std::make_unique<ClangStatASTConsumer>(&CI.getASTContext());
    }
};

int main(int argc, const char **argv) {
    // Accept two command line arguments, /path/to/compile_commands.json and /path/to/source_file
    tooling::CommonOptionsParser OptionsParser(argc, argv, ClangStatCategory);
    tooling::ClangTool StatTool(OptionsParser.getCompilations(),
                            OptionsParser.getSourcePathList());

    StatTool.clearArgumentsAdjusters();
    StatTool.appendArgumentsAdjuster(tooling::getClangStripOutputAdjuster());
    StatTool.appendArgumentsAdjuster(tooling::getClangStripDependencyFileAdjuster());

    StatTool.run(tooling::newFrontendActionFactory<ClangStatAction>().get());

    std::vector<std::string> sourcePathList = OptionsParser.getSourcePathList();
    std::string fileName = sourcePathList[0];
    const char* outputFileName = "file_info.json";

    // Dump json file
    json_object_object_add(json_file, "FileName", json_object_new_string(fileName.c_str()));
    json_object_object_add(json_file, "Info", info_array);
    json_object_to_file(outputFileName, json_file);
}
