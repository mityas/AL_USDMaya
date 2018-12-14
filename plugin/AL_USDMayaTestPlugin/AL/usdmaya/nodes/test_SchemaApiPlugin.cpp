//
// Copyright 2017 Animal Logic
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.//
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
#include "test_usdmaya.h"
#include "AL/usdmaya/nodes/ProxyShape.h"
#include "AL/usdmaya/nodes/Transform.h"
#include "AL/usdmaya/nodes/Layer.h"
#include "AL/usdmaya/StageCache.h"
#include "maya/MFnTransform.h"
#include "maya/MSelectionList.h"
#include "maya/MGlobal.h"
#include "maya/MItDependencyNodes.h"
#include "maya/MDagModifier.h"
#include "maya/MFileIO.h"
#include "AL/usdmaya/fileio/translators/TranslatorTestType.h"
#include "AL/usdmaya/fileio/translators/SchemaApiTestType.h"
#include "AL/usdmaya/fileio/translators/SchemaApiTestPlugin.h"

#include "pxr/usd/usd/stage.h"
#include "pxr/usd/sdf/types.h"
#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/usdGeom/xform.h"
#include "pxr/usd/usdGeom/xformCommonAPI.h"

#include <fstream>

using namespace AL::usdmaya::fileio::translators;

const char* g_schemaApiTestFile = 
"#usda 1.0\n"
"\n"
"def AL::usdmaya::fileio::translators::TranslatorTestType \"testPrim\" (\n"
"    prepend apiSchemas = [\"AL::usdmaya::fileio::translators::SchemaApiTestType\"]\n"
")\n"
"{\n"
"}\n";

TEST(ScheamApiPlugin, ScheamApiPlugin)
{
  const std::string filePath = buildTempPath("AL_USDMayaTests_schemaApi.usda");
  {
    std::ofstream ofs(filePath);
    ASSERT_TRUE(ofs);
    ofs << g_schemaApiTestFile;
  }

  {
    // create a TranslatorTestType usd prim
    UsdStageRefPtr m_stage = UsdStage::CreateInMemory();
    TranslatorTestType testPrim = TranslatorTestType::Define(m_stage, SdfPath("/testPrim"));
    UsdPrim m_prim = testPrim.GetPrim();
    SchemaApiTestType::Apply(m_prim);
    m_stage->GetRootLayer()->Export(filePath);
  }


  MFileIO::newFile(true);
  auto status = MGlobal::executeCommand(MString("AL_usdmaya_ProxyShapeImport -file \"") + filePath.c_str() + MString("\""));
  EXPECT_EQ(MS::kSuccess, status);

  // select the shape 
  MSelectionList sl;
  EXPECT_TRUE(sl.add("AL_usdmaya_ProxyShape"));
  MObject node;
  sl.getDependNode(0, node);
  ASSERT_TRUE(status);
  
  // grab pointer to it
  MFnDependencyNode fn(node, &status);
  ASSERT_TRUE(status);
  AL::usdmaya::nodes::ProxyShape* shape = (AL::usdmaya::nodes::ProxyShape*)fn.userNode();
  ASSERT_TRUE(shape);

  // now we can access the translator context

  auto manufacture = shape->translatorManufacture();
  auto context = shape->context();
  ASSERT_TRUE(context);

  MFnDagNode fnSilly;
  MObject mayaTM = fnSilly.create("transform");
  MObject mayaObject = fnSilly.create("distanceDimShape", mayaTM);
  EXPECT_TRUE(mayaObject.hasFn(MFn::kDistance));

  auto apis = manufacture.getAPI(mayaObject);
  ASSERT_TRUE(!apis.empty());
  EXPECT_EQ(1, apis.size());

  // ensure correct api plugin returned
  auto first = apis[0];
  EXPECT_EQ(MFn::kDistance, first->getFnType());

  typedef TfRefPtr<AL::usdmaya::fileio::translators::TestSchemaPlugin> TestSchemaPluginPtr;
  auto pptr = TfStatic_cast<TestSchemaPluginPtr>(first);

  EXPECT_TRUE(pptr->importCalled);
  EXPECT_TRUE(pptr->postImportCalled);
  EXPECT_TRUE(pptr->initialiseCalled);
  EXPECT_FALSE(pptr->exportObjectCalled);
  EXPECT_FALSE(pptr->preTearDownCalled);
  EXPECT_FALSE(pptr->updateCalled);

  pptr->importCalled = false;
  pptr->postImportCalled = false;
  pptr->initialiseCalled = false;

  // grab the stage and deactivate the prim
  auto stage = shape->usdStage();
  auto prim = stage->GetPrimAtPath(SdfPath("/testPrim"));
  prim.SetActive(false);

  EXPECT_FALSE(pptr->importCalled);
  EXPECT_FALSE(pptr->postImportCalled);
  EXPECT_FALSE(pptr->initialiseCalled);
  EXPECT_FALSE(pptr->exportObjectCalled);
  EXPECT_TRUE(pptr->preTearDownCalled);
  EXPECT_FALSE(pptr->updateCalled);

  pptr->initialiseCalled = false;
  pptr->preTearDownCalled = false;
  prim.SetActive(true);

  EXPECT_TRUE(pptr->importCalled);
  EXPECT_TRUE(pptr->postImportCalled);
  EXPECT_FALSE(pptr->initialiseCalled);
  EXPECT_FALSE(pptr->exportObjectCalled);
  EXPECT_FALSE(pptr->preTearDownCalled);
  EXPECT_FALSE(pptr->updateCalled);
  prim.SetActive(false);
  pptr->importCalled = false;
  pptr->postImportCalled = false;
  pptr->preTearDownCalled = false;

  sl.clear();
  sl.add(mayaTM);
  MGlobal::setActiveSelectionList(sl);

  // lets see if we can export the schema plugin 
  const std::string temp_path = buildTempPath("AL_USDMayaTests_schemaplugin.usda");
  MString command =
  "file -force -options "
  "\"Merge_Transforms=1;\" -typ \"AL usdmaya export\" -pr -ea \"";
  command += temp_path.c_str();
  command += "\";";
  MGlobal::executeCommand(command);

  // we can't test the translators to see if export has been called, since the export would use a different context,
  // and so the api schema plugin used will be a new instance :(
  // As a result, load the usda file that was exported, and see if the api has been applied to the prim
  auto stg = UsdStage::Open(temp_path);
  ASSERT_TRUE(stg);
  prim = stg->GetPrimAtPath(SdfPath("/transform1"));
  ASSERT_TRUE(prim);

  TfTokenVector v = prim.GetAppliedSchemas();
  ASSERT_EQ(1, v.size());
  EXPECT_EQ(v[0].GetString(), "AL::usdmaya::fileio::translators::SchemaApiTestType");
}