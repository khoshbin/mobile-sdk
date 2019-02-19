#include "NMLModelDrawData.h"
#include "geometry/Geometry.h"
#include "projections/Projection.h"
#include "projections/ProjectionSurface.h"
#include "vectorelements/NMLModel.h"
#include "utils/Const.h"

namespace carto {

    NMLModelDrawData::NMLModelDrawData(const NMLModel& model, const Projection& projection, const ProjectionSurface& projectionSurface) :
        VectorElementDrawData(Color()),
        _sourceModel(model.getSourceModel()),
        _localMat()
    {
        MapPos mapPosInternal = projection.toInternal(model.getGeometry()->getCenterPos());
        cglib::vec3<double> pos = projectionSurface.calculatePosition(mapPosInternal);
        cglib::mat4x4<double> rotateMat = cglib::rotate4_matrix(cglib::vec3<double>(model.getRotationAxis().getX(), model.getRotationAxis().getY(), model.getRotationAxis().getZ()), model.getRotationAngle() * Const::DEG_TO_RAD);
        cglib::mat4x4<double> scaleMat = cglib::scale4_matrix(cglib::vec3<double>(model.getScale(), model.getScale(), model.getScale()));
        _localMat = projectionSurface.calculateLocalFrameMatrix(pos) * rotateMat * scaleMat;
    }

    NMLModelDrawData::~NMLModelDrawData() {
    }
    
    std::shared_ptr<nml::Model> NMLModelDrawData::getSourceModel() const {
        return _sourceModel;
    }
    
    const cglib::mat4x4<double>& NMLModelDrawData::getLocalMat() const {
        return _localMat;
    }
    
    void NMLModelDrawData::offsetHorizontally(double offset) {
        _localMat = cglib::translate4_matrix(cglib::vec3<double>(offset, 0, 0)) * _localMat;
        setIsOffset(true);
    }

}
