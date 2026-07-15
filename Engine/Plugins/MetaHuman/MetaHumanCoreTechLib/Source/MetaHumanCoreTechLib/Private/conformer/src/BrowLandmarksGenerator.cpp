// Copyright Epic Games, Inc. All Rights Reserved.

#include <conformer/BrowLandmarksGenerator.h>
#include <nls/geometry/Polyline.h>
#include <carbon/Common.h>

CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

template <class T>
BrowLandmarksGenerator<T>::BrowLandmarksGenerator() {}

template <class T> BrowLandmarksGenerator<T>::~BrowLandmarksGenerator() = default;

template <class T>
void BrowLandmarksGenerator<T>::Init(const TemplateDescription& templateDesc)
{
    m_topology = templateDesc.Topology();
    m_templateMeshLandmarks = templateDesc.GetMeshLandmarks();
    const SymmetryMapping& symmetryMapping = templateDesc.GetSymmetryMapping();
    m_browMaskL = templateDesc.GetVertexWeights("brow_mask_l").NonzeroVertices();
    for (int vID : m_browMaskL)
    {
        m_browMaskR.push_back(symmetryMapping.Map(vID));
    }
}

template <class T>
void BrowLandmarksGenerator<T>::SetLandmarks(const std::pair<LandmarkInstance<T, 2>, Camera<T>>& landmarks) { m_landmarks = landmarks; }

template <class T>
bool BrowLandmarksGenerator<T>::CheckLandmarkInBrowsMask(const Eigen::Matrix<T, 3, -1>& vertices, const Camera<T>& camera, const Affine<T, 3, 3>& mesh2scanTransform,
	const T mesh2ScanScale, const Mesh<T>& browMesh, const Eigen::Matrix<T, 2, -1>& curvePts, int ptIndex, BarycentricCoordinates<T>& landmarkBarycentricCoordinates)
{
    if (curvePts.cols() == 0)
    {
        return false;
    }

    Eigen::Matrix<T, 2, 1> actualLandmark = curvePts.col(ptIndex);
    const Eigen::Vector3<T> actualLandmark3D{ actualLandmark(0), actualLandmark(1), 0};

    const Eigen::Matrix<int, 4, -1>& browQuads = browMesh.Quads();
    const Eigen::Matrix<int, 3, -1>& browTriangles = browMesh.Triangles();

    Eigen::Matrix<int, 3, -1> allBrowTriangles(3, browTriangles.cols() + browQuads.cols() * 2);
    allBrowTriangles.leftCols(browTriangles.cols()) = browTriangles;

    // convert the quads into triangles
    int col = static_cast<int>(browTriangles.cols());
    for (int quad = 0; quad < browQuads.cols(); ++quad)
    {
        allBrowTriangles(0, col) = browQuads(0, quad);
        allBrowTriangles(1, col) = browQuads(1, quad);
        allBrowTriangles(2, col) = browQuads(2, quad);

        allBrowTriangles(0, col + 1) = browQuads(2, quad);
        allBrowTriangles(1, col + 1) = browQuads(3, quad);
        allBrowTriangles(2, col + 1) = browQuads(0, quad);

        col += 2;
    }

    // go through each triangle, and project into 2D. Find the first one which the landmark is inside (a heuristic, but nearly certain to be the case
    // as the brows are calculated from a frontal view).
    for (int tri = 0; tri < allBrowTriangles.cols(); ++tri)
    {
        const Eigen::Vector2<T> pix0 = camera.Project(Eigen::Vector3<T>(mesh2ScanScale * mesh2scanTransform.Transform(vertices.col(allBrowTriangles(0, tri)))),
            /*withExtrinsics=*/true);
        const Eigen::Vector2<T> pix1 = camera.Project(Eigen::Vector3<T>(mesh2ScanScale * mesh2scanTransform.Transform(vertices.col(allBrowTriangles(1, tri)))),
            /*withExtrinsics=*/true);
        const Eigen::Vector2<T> pix2 = camera.Project(Eigen::Vector3<T>(mesh2ScanScale * mesh2scanTransform.Transform(vertices.col(allBrowTriangles(2, tri)))),
            /*withExtrinsics=*/true);

        // turn these into 3D points with Z = 0 and calculate the barycentric coodinates. If all BCs are between 0 and 1 we are inside the triangle
        const Eigen::Vector3<T> pix03D{ pix0(0), pix0(1), 0 };
        const Eigen::Vector3<T> pix13D{ pix1(0), pix1(1), 0 };
        const Eigen::Vector3<T> pix23D{ pix2(0), pix2(1), 0 };

        Eigen::Vector<int, 3> indices = { 0, 1, 2 };
        Eigen::Matrix<T, 3, -1> triVerts(3, 3);
        triVerts.col(0) = pix03D;
        triVerts.col(1) = pix13D;
        triVerts.col(2) = pix23D;

        Eigen::Vector<T, 3> bcWeights = BarycentricCoordinates<T, 3>::ComputeBarycentricCoordinates(actualLandmark3D, indices, triVerts);
        if (bcWeights[0] >= 0 && bcWeights[0] <= 1 &&
            bcWeights[1] >= 0 && bcWeights[1] <= 1 &&
            bcWeights[2] >= 0 && bcWeights[2] <= 1)
        {
            landmarkBarycentricCoordinates = BarycentricCoordinates<T>{ allBrowTriangles.col(tri), bcWeights};
            return true;
        }
    }

    return false;
}

template <class T>
MeshLandmarks<T> BrowLandmarksGenerator<T>::Generate(const Eigen::Matrix<T, 3, -1>& vertices, 
                                                                const Affine<T, 3, 3>& mesh2scanTransform,
                                                                const T mesh2ScanScale,
                                                                float &fractionLandmarksInBrowRegion,
                                                                bool concatenate)
{
    const std::vector<std::pair<int, int>> browEdgesL = m_topology.GetEdges(m_browMaskL);
    const std::vector<std::pair<int, int>> browEdgesR = m_topology.GetEdges(m_browMaskR);
    Mesh<T> browMeshL = m_topology;
    browMeshL.Mask(m_browMaskL);
    Mesh<T> browMeshR = m_topology;
    browMeshR.Mask(m_browMaskR);

    const auto& landmarks = m_landmarks.first;
    const auto& camera = m_landmarks.second;
    std::shared_ptr<const LandmarkConfiguration> landmarkConfiguration = landmarks.GetLandmarkConfiguration();

    MeshLandmarks<T> browLandmarks;
    MeshLandmarks<T> allHeadMeshLandmarks = m_templateMeshLandmarks;
    unsigned totalPoints = 0;
    unsigned validPoints = 0;

    struct CurveNameAndLandmarks
    {
        std::string CurveName;
        std::string LandmarkNameStart;
        std::string LandmarkNameEnd;
    };

    std::vector<CurveNameAndLandmarks> curveAndLandmarkNamesVec
    {
        { "crv_brow_lower", "", ""},
        { "crv_brow_upper", "", "pt_brow_outer"},
        { "crv_brow_intermediate", "pt_brow_inner", "pt_brow_intermediate"},
    };

    for (bool left : { true, false })
    {
        const std::vector<std::pair<int, int>>& browEdges = left ? browEdgesL : browEdgesR;
        const Mesh<T>& browMesh = left ? browMeshL : browMeshR;

        for (const CurveNameAndLandmarks& curveAndLandmarkNames : curveAndLandmarkNamesVec)
        {
            const std::string extCurveName = curveAndLandmarkNames.CurveName + (left ? "_l" : "_r");

            if (landmarkConfiguration->HasCurve(extCurveName))
            {
                const Eigen::Matrix<T, 2, -1> curvePts = landmarks.Points(landmarkConfiguration->IndicesForCurve(extCurveName));
                const Polyline<T, 2> polyline(curvePts);
                std::vector<bool> controlPointUsed(static_cast<unsigned>(polyline.NumControlPoints()), false);
                std::vector<BarycentricCoordinates<T>> barycentricCoordinates;
                for (const auto& [vID0, vID1] : browEdges)
                {
                    const Eigen::Vector2<T> pix0 = camera.Project(Eigen::Vector3<T>(mesh2ScanScale * mesh2scanTransform.Transform(vertices.col(vID0))),
                        /*withExtrinsics=*/true);
                    const Eigen::Vector2<T> pix1 = camera.Project(Eigen::Vector3<T>(mesh2ScanScale * mesh2scanTransform.Transform(vertices.col(vID1))),
                        /*withExtrinsics=*/true);

                    const std::vector<std::pair<T, int>> intersections = polyline.FindIntersections(pix0, pix1);
                    if (intersections.size() > 0)
                    {
                        const T alpha = intersections[0].first;
                        controlPointUsed[static_cast<unsigned>(intersections[0].second)] = true;
                        controlPointUsed[static_cast<unsigned>(intersections[0].second + 1)] = true;
                        barycentricCoordinates.push_back(BarycentricCoordinates<T>(Eigen::Vector3i(vID0, vID1, vID1),
                            Eigen::Vector3<T>((1.0f - alpha), alpha, 0)));
                    }
                }
                totalPoints += polyline.NumControlPoints();
                for (const bool& pointFlag : controlPointUsed)
                {
                    if (pointFlag)
                    {
                        validPoints ++;
                    }
                }
                browLandmarks.AddCurve(extCurveName, barycentricCoordinates);
                if (allHeadMeshLandmarks.HasCurve(extCurveName))
                {
                    LOG_WARNING("Template mesh landmarks already contain curve {}", extCurveName);
                }
                allHeadMeshLandmarks.AddCurve(extCurveName, barycentricCoordinates);


                // add additional point 2 point constraints if needed
                BarycentricCoordinates<T> landmarkBarycentricCoordinates;

                if (!curveAndLandmarkNames.LandmarkNameStart.empty())
                {
                    const std::string extLandmarkName = curveAndLandmarkNames.LandmarkNameStart + (left ? "_l" : "_r");
                    const bool foundStartLandmark = CheckLandmarkInBrowsMask(vertices, camera, mesh2scanTransform, mesh2ScanScale, browMesh, curvePts, 0, landmarkBarycentricCoordinates);
                    totalPoints++;
                    if (foundStartLandmark)
                    {
                        browLandmarks.AddLandmark(extLandmarkName, landmarkBarycentricCoordinates);
                        if (allHeadMeshLandmarks.HasLandmark(extLandmarkName))
                        {
                            LOG_WARNING("Template mesh landmarks already contain landmark {}", extLandmarkName);
                        }
                        allHeadMeshLandmarks.AddLandmark(extLandmarkName, landmarkBarycentricCoordinates);
                        validPoints++;
                    }
                }

                if (!curveAndLandmarkNames.LandmarkNameEnd.empty())
                {
                    const std::string extLandmarkName = curveAndLandmarkNames.LandmarkNameEnd + (left ? "_l" : "_r");
                    const bool foundEndLandmark = CheckLandmarkInBrowsMask(vertices, camera, mesh2scanTransform, mesh2ScanScale, browMesh, curvePts, static_cast<int>(curvePts.cols() - 1), landmarkBarycentricCoordinates);
                    totalPoints++;
                    if (foundEndLandmark)
                    {
                        browLandmarks.AddLandmark(extLandmarkName, landmarkBarycentricCoordinates);
                        if (allHeadMeshLandmarks.HasLandmark(extLandmarkName))
                        {
                            LOG_WARNING("Template mesh landmarks already contain landmark {}", extLandmarkName);
                        }
                        allHeadMeshLandmarks.AddLandmark(extLandmarkName, landmarkBarycentricCoordinates);
                        validPoints++;
                    }
                }

            }
        }
    }

    fractionLandmarksInBrowRegion = 1.0f;
    if (totalPoints > 0)
    {
        fractionLandmarksInBrowRegion = static_cast<float>(validPoints) / static_cast<float>(totalPoints);
    }
    return concatenate ? allHeadMeshLandmarks : browLandmarks;
}

template class BrowLandmarksGenerator<float>;

CARBON_NAMESPACE_END(TITAN_NAMESPACE)
