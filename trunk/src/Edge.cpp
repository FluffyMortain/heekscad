// Edge.cpp
#include "stdafx.h"
#include "Edge.h"
#include "Face.h"
#include "Solid.h"
#include "../interface/Tool.h"
#include <BRepMesh.hxx>
#include <TColgp_Array1OfDir.hxx>
#include <BRepTools.hxx>
#include <Poly_Polygon3D.hxx>
#include <Poly_PolygonOnTriangulation.hxx>
#include <Poly_Triangulation.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_ListIteratorOfListOfShape.hxx>
#include <TopExp.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepAdaptor_Curve.hxx>

wxIcon* CEdge::m_icon = NULL;

CEdge::CEdge(const TopoDS_Edge &edge):m_topods_edge(edge){
#if _DEBUG
	GetCurveParams2(&m_start_u, &m_end_u, &m_isClosed, &m_isPeriodic);
	Evaluate(m_start_u, &m_start_x, &m_start_tangent_x);
	double t[3];
	Evaluate(m_end_u, &m_end_x, t);
	m_orientation = Orientation();
#endif
}

CEdge::~CEdge(){
}

void CEdge::glCommands(bool select, bool marked, bool no_color){
	glColor3ub(0, 0, 0);

	if(m_owner && m_owner->m_owner && m_owner->m_owner->GetType() == SolidType)
	{
		// triangulate a face on the edge first
		TopTools_IndexedDataMapOfShapeListOfShape lface;
		TopExp::MapShapesAndAncestors(((CShape*)(m_owner->m_owner))->Shape(),TopAbs_EDGE,TopAbs_FACE,lface);
		const TopTools_ListOfShape& lfac = lface.FindFromKey(m_topods_edge);
		Standard_Integer nelem= lfac.Extent();
		if(nelem == 2){
			TopTools_ListIteratorOfListOfShape It;
			It.Initialize(lfac);
			TopoDS_Face Face1 = TopoDS::Face(It.Value());
			TopLoc_Location fL;
			Handle_Poly_Triangulation facing = BRep_Tool::Triangulation(Face1,fL);

			if(!facing.IsNull())
			{
				// Get polygon
				Handle_Poly_PolygonOnTriangulation polygon = BRep_Tool::PolygonOnTriangulation(m_topods_edge, facing, fL);
				gp_Trsf tr = fL;
				double m[16];
				extract_transposed(tr, m);
				glPushMatrix();
				glMultMatrixd(m);

				if (!polygon.IsNull())
				{
					glBegin(GL_LINE_STRIP);
					const TColStd_Array1OfInteger& Nodes = polygon->Nodes();
					const TColgp_Array1OfPnt& FNodes = facing->Nodes();
					int nnn = polygon->NbNodes();
					for (int nn = 1; nn <= nnn; nn++)
					{
						gp_Pnt v = FNodes(Nodes(nn));
						glVertex3d(v.X(), v.Y(), v.Z());
					}
					glEnd();
				}

				glPopMatrix();
			}
		}
	}
	else
	{
		TopLoc_Location L;
		Handle(Poly_Polygon3D) Polyg = BRep_Tool::Polygon3D(m_topods_edge, L);
		if (!Polyg.IsNull()) {
			const TColgp_Array1OfPnt& Points = Polyg->Nodes();
			Standard_Integer po;
			glBegin(GL_LINE_STRIP);
			for (po = Points.Lower(); po <= Points.Upper(); po++) {
				gp_Pnt p = (Points.Value(po)).Transformed(L);
				glVertex3d(p.X(), p.Y(), p.Z());
			}
			glEnd();
		}
	}
}

void CEdge::GetBox(CBox &box){
}

wxIcon* CEdge::GetIcon(){
	if(m_icon == NULL)
	{
		wxString exe_folder = wxGetApp().GetExeFolder();
		m_icon = new wxIcon(exe_folder + _T("/icons/edge.png"), wxBITMAP_TYPE_PNG);
	}
	return m_icon;
}

class BlendTool:public Tool
{
public:
	CEdge* m_edge;

	BlendTool(CEdge* edge):m_edge(edge){}

	const wxChar* GetTitle(){return _T("Blend");}
	void Run(){
		double rad = 2.0;
		wxGetApp().m_config->Read(_T("EdgeBlendRadius"), &rad);
		if(wxGetApp().InputDouble(_T("Enter Blend Radius"), _T("Radius"), rad))
		{
			m_edge->Blend(rad);
			wxGetApp().m_config->Write(_T("EdgeBlendRadius"), rad);
		}
	}
};

static BlendTool blend_tool(NULL);

void CEdge::GetTools(std::list<Tool*>* t_list, const wxPoint* p){
	if(m_owner && m_owner->m_owner && m_owner->m_owner->GetType() == SolidType)
		blend_tool.m_edge = this;
		t_list->push_back(&blend_tool);
}

void CEdge::Blend(double radius){
	try{
		if(m_owner && m_owner->m_owner && CShape::IsTypeAShape(m_owner->m_owner->GetType())){
			BRepFilletAPI_MakeFillet fillet(((CShape*)(m_owner->m_owner))->Shape());
			fillet.Add(radius, m_topods_edge);
			TopoDS_Shape new_shape = fillet.Shape();
			wxGetApp().StartHistory(_T("Blending Edge"));
			wxGetApp().AddUndoably(new CSolid(*((TopoDS_Solid*)(&new_shape)), _T("Edge Blended Solid")), NULL, NULL);
			wxGetApp().DeleteUndoably(m_owner->m_owner);
			wxGetApp().EndHistory();
		}
	}
	catch(wchar_t *message)
	{
		wxMessageBox(_T("A fatal error happened during Blend -\n") + wxString(message));
	}
	catch(...)
	{
		wxMessageBox(_T("A fatal error happened during Blend"));
	}
}

void CEdge::WriteXML(TiXmlElement *root)
{
	CShape::m_solids_found = true;
}

CFace* CEdge::GetFirstFace()
{
	if (m_faces.size()==0) return NULL;
	m_faceIt = m_faces.begin();
	return *m_faceIt;
}

CFace* CEdge::GetNextFace()
{
	if (m_faces.size()==0 || m_faceIt==m_faces.end()) return NULL;
	m_faceIt++;
	if (m_faceIt==m_faces.end()) return NULL;
	return *m_faceIt;
}

int CEdge::GetCurveType()
{
	// enum GeomAbs_CurveType
	// 0 - GeomAbs_Line
	// 1 - GeomAbs_Circle
	// 2 - GeomAbs_Ellipse
	// 3 - GeomAbs_Hyperbola
	// 4 - GeomAbs_Parabola
	// 5 - GeomAbs_BezierCurve
	// 6 - GeomAbs_BSplineCurve
	// 7 - GeomAbs_OtherCurve

	BRepAdaptor_Curve curve(m_topods_edge);
	GeomAbs_CurveType curve_type = curve.GetType();
	return curve_type;
}

void CEdge::GetCurveParams(double* start, double* end, double* uStart, double* uEnd, int* Reversed)
{
	BRepAdaptor_Curve curve(m_topods_edge);
	double us = curve.FirstParameter();
	double ue = curve.LastParameter();
	if(uStart)*uStart = us;
	if(uEnd)*uEnd = ue;
	if(start)extract(curve.Value(us), start);
	if(end)extract(curve.Value(ue), end);
	if(Reversed)*Reversed = Orientation() ? 0:1;
}

void CEdge::GetCurveParams2(double *uStart, double *uEnd, int *isClosed, int *isPeriodic)
{
	BRepAdaptor_Curve curve(m_topods_edge);
	*uStart = curve.FirstParameter();
	*uEnd = curve.LastParameter();
	*isClosed = curve.IsClosed();
	*isPeriodic = curve.IsPeriodic();
}

bool CEdge::InFaceSense(CFace* face)
{
	std::list<CFace*>::iterator FIt = m_faces.begin();
	std::list<bool>::iterator FSIt = m_face_senses.begin();
	for(;FIt != m_faces.end(); FIt++, FSIt++)
	{
		CFace* f = *FIt;
		bool sense = *FSIt;

		if(f == face)
		{
			bool eo = Orientation();
			return (sense == eo);
		}
	}

	return false; // shouldn't get here
}

void CEdge::Evaluate(double u, double *p, double *tangent)
{
	BRepAdaptor_Curve curve(m_topods_edge);
	gp_Pnt P;
	gp_Vec V;
	curve.D1(u, P, V);
	extract(P, p);
	extract(V, tangent);
}

bool CEdge::Orientation()
{
	TopAbs_Orientation o = m_topods_edge.Orientation();
	return (o == TopAbs_FORWARD);
}