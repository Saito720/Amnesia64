#ifndef MULTIPLAYER_SKELETON_INTERPOLATION_REGRESSION_H
#define MULTIPLAYER_SKELETON_INTERPOLATION_REGRESSION_H

#include "LuxPlayerHands.h"

inline bool RunSkeletonInterpolationRegression(tString& error)
{
    cMeshEntity* mesh = gpBase->mpPlayer->GetHands()->GetHandsEntity();
    if (!mesh || !mesh->GetBoneStateNum() || !mesh->GetSubMeshEntityNum())
    { error = "skeletal interpolation fixture needs the native player hands"; return false; }
    cSubMeshEntity* surface = mesh->GetSubMeshEntity(0);
    const cMatrixf root = mesh->GetLocalMatrix();
    std::vector<cMatrixf> bones;
    for (int i = 0; i < mesh->GetBoneStateNum(); ++i) bones.push_back(mesh->GetBoneState(i)->GetLocalMatrix());
    bool passed = true;
    const auto require = [&](bool condition, const char* message) {
        if (!condition && passed) { error = message; passed = false; }
    };
    const auto vertices = [&]() {
        iVertexBuffer* buffer = surface->GetVertexBuffer();
        const float* first = buffer->GetFloatArray(eVertexBufferElement_Position);
        return std::vector<float>(first, first + buffer->GetVertexNum() * buffer->GetElementNum(eVertexBufferElement_Position));
    };
    iEntity3D::BeginRenderInterpolation(1);
    surface->UpdateGraphicsForFrame(0);
    iEntity3D::EndRenderInterpolation();
    mesh->ResetRenderInterpolation();
    iEntity3D::CaptureInterpolationState();
    int stationary = 0;
    std::vector<float> stationaryVertices;
    for (int frame = 0; frame < 32; ++frame)
    {
        iEntity3D::BeginRenderInterpolation((frame % 5) * 0.25f);
        const int revision = surface->GetMatrixUpdateCount();
        surface->UpdateGraphicsForFrame(1.0f / 240);
        if (!frame) { stationary = revision; stationaryVertices = vertices(); }
        require(revision == stationary, "stationary skeleton invalidated skinning/shadow caches for another rendered frame");
        require(vertices() == stationaryVertices, "stationary skeletal render samples changed the vertex buffer");
        iEntity3D::EndRenderInterpolation();
    }
    iEntity3D::CaptureInterpolationState();
    for (int i = 0; i < mesh->GetBoneStateNum(); ++i)
        mesh->GetBoneState(i)->SetPosition(bones[i].GetTranslation() + cVector3f(0.1f, 0, 0));
    int movingRevision = stationary;
    std::vector<float> movingVertices;
    for (float alpha : {0.25f, 0.75f})
    {
        iEntity3D::BeginRenderInterpolation(alpha);
        const int revision = surface->GetMatrixUpdateCount();
        surface->UpdateGraphicsForFrame(1.0f / 240);
        require(revision != movingRevision, "bone motion failed to invalidate skinning/shadow caches between render samples");
        const auto rendered = vertices();
        require(rendered != (movingVertices.empty() ? stationaryVertices : movingVertices),
            "native skinned vertices did not move between interpolated bone poses");
        movingRevision = revision; movingVertices = rendered;
        for (int i = 0; i < mesh->GetBoneStateNum(); ++i)
            require(mesh->GetBoneState(i)->GetLocalPosition() == bones[i].GetTranslation() + cVector3f(0.1f, 0, 0),
                "skeletal presentation changed the authoritative bone transforms");
        iEntity3D::EndRenderInterpolation();
    }
    iEntity3D::CaptureInterpolationState();
    int settled = 0;
    for (float alpha : {0.0f, 0.25f, 0.75f, 1.0f})
    {
        iEntity3D::BeginRenderInterpolation(alpha);
        const int revision = surface->GetMatrixUpdateCount();
        surface->UpdateGraphicsForFrame(1.0f / 240);
        if (!settled) settled = revision;
        require(revision == settled && revision != movingRevision,
            "skeleton must finish its final interval once, then reuse the settled pose");
        iEntity3D::EndRenderInterpolation();
    }
    mesh->SetPosition(root.GetTranslation() + cVector3f(1, 0, 0));
    iEntity3D::BeginRenderInterpolation(0.5f);
    require(surface->GetMatrixUpdateCount() != settled, "moving the whole skeleton failed to invalidate its shadow cache");
    iEntity3D::EndRenderInterpolation();

    mesh->SetMatrix(root);
    for (int i = 0; i < mesh->GetBoneStateNum(); ++i) mesh->GetBoneState(i)->SetMatrix(bones[i]);
    gpBase->mpEngine->GetScene()->ResetInterpolationState();
    surface->UpdateGraphicsForFrame(0);
    return passed;
}

#endif
