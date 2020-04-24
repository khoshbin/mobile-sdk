#include "TileRenderer.h"
#include "components/Options.h"
#include "components/ThreadWorker.h"
#include "graphics/ViewState.h"
#include "projections/ProjectionSurface.h"
#include "projections/PlanarProjectionSurface.h"
#include "renderers/MapRenderer.h"
#include "renderers/drawdatas/TileDrawData.h"
#include "renderers/utils/GLResourceManager.h"
#include "renderers/utils/VTRenderer.h"
#include "utils/Log.h"

#include <vt/Label.h>
#include <vt/LabelCuller.h>
#include <vt/TileTransformer.h>
#include <vt/GLTileRenderer.h>
#include <vt/GLExtensions.h>

#include <cglib/mat.h>

namespace carto {
    
    TileRenderer::TileRenderer() :
        _mapRenderer(),
        _options(),
        _tileTransformer(),
        _vtRenderer(),
        _interactionMode(false),
        _subTileBlending(true),
        _labelOrder(0),
        _buildingOrder(1),
        _horizontalLayerOffset(0),
        _viewDir(0, 0, 0),
        _mainLightDir(0, 0, 0),
        _tiles(),
        _mutex()
    {
    }
    
    TileRenderer::~TileRenderer() {
    }
    
    void TileRenderer::setComponents(const std::weak_ptr<Options>& options, const std::weak_ptr<MapRenderer>& mapRenderer) {
        std::lock_guard<std::mutex> lock(_mutex);
        _options = options;
        _mapRenderer = mapRenderer;
        _vtRenderer.reset();
    }

    std::shared_ptr<vt::TileTransformer> TileRenderer::getTileTransformer() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _tileTransformer;
    }

    void TileRenderer::setTileTransformer(const std::shared_ptr<vt::TileTransformer>& tileTransformer) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_tileTransformer != tileTransformer) {
            _vtRenderer.reset();
        }
        _tileTransformer = tileTransformer;
    }
    
    void TileRenderer::setInteractionMode(bool enabled) {
        std::lock_guard<std::mutex> lock(_mutex);
        _interactionMode = enabled;
    }
    
    void TileRenderer::setSubTileBlending(bool enabled) {
        std::lock_guard<std::mutex> lock(_mutex);
        _subTileBlending = enabled;
    }

    void TileRenderer::setLabelOrder(int order) {
        std::lock_guard<std::mutex> lock(_mutex);
        _labelOrder = order;
    }
    
    void TileRenderer::setBuildingOrder(int order) {
        std::lock_guard<std::mutex> lock(_mutex);
        _buildingOrder = order;
    }

    void TileRenderer::offsetLayerHorizontally(double offset) {
        std::lock_guard<std::mutex> lock(_mutex);
        _horizontalLayerOffset += offset;
    }
    
    bool TileRenderer::onDrawFrame(float deltaSeconds, const ViewState& viewState) {
        std::lock_guard<std::mutex> lock(_mutex);
        
        if (!initializeRenderer()) {
            return false;
        }
        std::shared_ptr<vt::GLTileRenderer> tileRenderer = _vtRenderer->getTileRenderer();
        if (!tileRenderer) {
            return false;
        }

        cglib::mat4x4<double> modelViewMat = viewState.getModelviewMat() * cglib::translate4_matrix(cglib::vec3<double>(_horizontalLayerOffset, 0, 0));
        tileRenderer->setViewState(vt::ViewState(viewState.getProjectionMat(), modelViewMat, viewState.getZoom(), viewState.getAspectRatio(), viewState.getNormalizedResolution()));
        tileRenderer->setInteractionMode(_interactionMode);
        tileRenderer->setSubTileBlending(_subTileBlending);

        _viewDir = cglib::unit(viewState.getFocusPosNormal());
        if (auto options = _options.lock()) {
            _mainLightDir = cglib::vec3<float>::convert(cglib::unit(viewState.getProjectionSurface()->calculateVector(MapPos(0, 0), options->getMainLightDirection())));
        }

        tileRenderer->startFrame(deltaSeconds * 3);

        bool refresh = false;
        refresh = tileRenderer->renderGeometry2D() || refresh;
        if (_labelOrder == 0) {
            refresh = tileRenderer->renderLabels(true, false) || refresh;
        }
        if (_buildingOrder == 0) {
            refresh = tileRenderer->renderGeometry3D() || refresh;
        }
        if (_labelOrder == 0) {
            refresh = tileRenderer->renderLabels(false, true) || refresh;
        }
    
        // Reset GL state to the expected state
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);

        GLContext::CheckGLError("TileRenderer::onDrawFrame");
        return refresh;
    }
    
    bool TileRenderer::onDrawFrame3D(float deltaSeconds, const ViewState& viewState) {
        std::lock_guard<std::mutex> lock(_mutex);
        
        if (!_vtRenderer) {
            return false;
        }
        std::shared_ptr<vt::GLTileRenderer> tileRenderer = _vtRenderer->getTileRenderer();
        if (!tileRenderer) {
            return false;
        }

        bool refresh = false;
        if (_labelOrder == 1) {
            refresh = tileRenderer->renderLabels(true, false) || refresh;
        }
        if (_buildingOrder == 1) {
            refresh = tileRenderer->renderGeometry3D() || refresh;
        }
        if (_labelOrder == 1) {
            refresh = tileRenderer->renderLabels(false, true) || refresh;
        }

        tileRenderer->endFrame();

        // Reset GL state to the expected state
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);

        GLContext::CheckGLError("TileRenderer::onDrawFrame3D");
        return refresh;
    }
    
    bool TileRenderer::cullLabels(vt::LabelCuller& culler, const ViewState& viewState) {
        std::shared_ptr<vt::GLTileRenderer> tileRenderer;
        cglib::mat4x4<double> modelViewMat;
        {
            std::lock_guard<std::mutex> lock(_mutex);

            if (_vtRenderer) {
                tileRenderer = _vtRenderer->getTileRenderer();
            }
            modelViewMat = viewState.getModelviewMat() * cglib::translate4_matrix(cglib::vec3<double>(_horizontalLayerOffset, 0, 0));
        }

        if (!tileRenderer) {
            return false;
        }
        culler.setViewState(vt::ViewState(viewState.getProjectionMat(), modelViewMat, viewState.getZoom(), viewState.getAspectRatio(), viewState.getNormalizedResolution()));
        tileRenderer->cullLabels(culler);
        return true;
    }
    
    bool TileRenderer::refreshTiles(const std::vector<std::shared_ptr<TileDrawData> >& drawDatas) {
        std::lock_guard<std::mutex> lock(_mutex);

        std::map<vt::TileId, std::shared_ptr<const vt::Tile> > tiles;
        for (const std::shared_ptr<TileDrawData>& drawData : drawDatas) {
            tiles[drawData->getVTTileId()] = drawData->getVTTile();
        }

        bool changed = (tiles != _tiles) || (_horizontalLayerOffset != 0);
        if (!changed) {
            return false;
        }

        if (_vtRenderer) {
            if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = _vtRenderer->getTileRenderer()) {
                tileRenderer->setVisibleTiles(tiles, _horizontalLayerOffset == 0);
            }
        }
        _tiles = std::move(tiles);
        _horizontalLayerOffset = 0;
        return true;
    }

    void TileRenderer::calculateRayIntersectedElements(const cglib::ray3<double>& ray, const ViewState& viewState, float radius, std::vector<std::tuple<vt::TileId, double, long long> >& results) const {
        std::lock_guard<std::mutex> lock(_mutex);

        if (!_vtRenderer) {
            return;
        }
        std::shared_ptr<vt::GLTileRenderer> tileRenderer = _vtRenderer->getTileRenderer();
        if (!tileRenderer) {
            return;
        }

        tileRenderer->findGeometryIntersections(ray, results, radius, true, false);
        if (_labelOrder == 0) {
            tileRenderer->findLabelIntersections(ray, results, radius, true, false);
        }
        if (_buildingOrder == 0) {
            tileRenderer->findGeometryIntersections(ray, results, radius, false, true);
        }
        if (_labelOrder == 0) {
            tileRenderer->findLabelIntersections(ray, results, radius, false, true);
        }
    }
        
    void TileRenderer::calculateRayIntersectedElements3D(const cglib::ray3<double>& ray, const ViewState& viewState, float radius, std::vector<std::tuple<vt::TileId, double, long long> >& results) const {
        std::lock_guard<std::mutex> lock(_mutex);

        if (!_vtRenderer) {
            return;
        }
        std::shared_ptr<vt::GLTileRenderer> tileRenderer = _vtRenderer->getTileRenderer();
        if (!tileRenderer) {
            return;
        }

        if (_labelOrder == 1) {
            tileRenderer->findLabelIntersections(ray, results, radius, true, false);
        }
        if (_buildingOrder == 1) {
            tileRenderer->findGeometryIntersections(ray, results, radius, false, true);
        }
        if (_labelOrder == 1) {
            tileRenderer->findLabelIntersections(ray, results, radius, false, true);
        }
    }

    void TileRenderer::calculateRayIntersectedBitmaps(const cglib::ray3<double>& ray, const ViewState& viewState, std::vector<std::tuple<vt::TileId, double, vt::TileBitmap, cglib::vec2<float> > >& results) const {
        std::lock_guard<std::mutex> lock(_mutex);

        if (!_vtRenderer) {
            return;
        }
        std::shared_ptr<vt::GLTileRenderer> tileRenderer = _vtRenderer->getTileRenderer();
        if (!tileRenderer) {
            return;
        }

        tileRenderer->findBitmapIntersections(ray, results);
    }

    bool TileRenderer::initializeRenderer() {
        if (_vtRenderer && _vtRenderer->isValid()) {
            return true;
        }

        std::shared_ptr<MapRenderer> mapRenderer = _mapRenderer.lock();
        if (!mapRenderer) {
            return false; // safety check, should never happen
        }

        boost::optional<vt::GLTileRenderer::LightingShader> lightingShader2D;
        if (!std::dynamic_pointer_cast<PlanarProjectionSurface>(mapRenderer->getProjectionSurface())) {
            lightingShader2D = vt::GLTileRenderer::LightingShader(true, LIGHTING_SHADER_2D, [this](GLuint shaderProgram, const vt::ViewState& viewState) {
                glUniform3fv(glGetUniformLocation(shaderProgram, "u_viewDir"), 1, _viewDir.data());
            });
        }
        boost::optional<vt::GLTileRenderer::LightingShader> lightingShader3D = vt::GLTileRenderer::LightingShader(true, LIGHTING_SHADER_3D, [this](GLuint shaderProgram, const vt::ViewState& viewState) {
            if (auto options = _options.lock()) {
                const Color& ambientLightColor = options->getAmbientLightColor();
                glUniform4f(glGetUniformLocation(shaderProgram, "u_ambientColor"), ambientLightColor.getR() / 255.0f, ambientLightColor.getG() / 255.0f, ambientLightColor.getB() / 255.0f, ambientLightColor.getA() / 255.0f);
                const Color& mainLightColor = options->getMainLightColor();
                glUniform4f(glGetUniformLocation(shaderProgram, "u_lightColor"), mainLightColor.getR() / 255.0f, mainLightColor.getG() / 255.0f, mainLightColor.getB() / 255.0f, mainLightColor.getA() / 255.0f);
                glUniform3fv(glGetUniformLocation(shaderProgram, "u_lightDir"), 1, _mainLightDir.data());
                glUniform3fv(glGetUniformLocation(shaderProgram, "u_viewDir"), 1, _viewDir.data());
            }
        });

        Log::Debug("TileRenderer: Initializing renderer");
        _vtRenderer = mapRenderer->getGLResourceManager()->create<VTRenderer>(_tileTransformer, lightingShader2D, lightingShader3D);
        if (std::shared_ptr<vt::GLTileRenderer> tileRenderer = _vtRenderer->getTileRenderer()) {
            tileRenderer->setVisibleTiles(_tiles, _horizontalLayerOffset == 0);
        }

        return _vtRenderer && _vtRenderer->isValid();
    }

    const std::string TileRenderer::LIGHTING_SHADER_2D = R"GLSL(
        uniform vec3 u_viewDir;
        vec4 applyLighting(vec4 color, vec3 normal) {
            float lighting = max(0.0, dot(normal, u_viewDir)) * 0.5 + 0.5;
            return vec4(color.rgb * lighting, color.a);
        }
    )GLSL";

    const std::string TileRenderer::LIGHTING_SHADER_3D = R"GLSL(
        uniform vec4 u_ambientColor;
        uniform vec4 u_lightColor;
        uniform vec3 u_lightDir;
        uniform vec3 u_viewDir;
        vec4 applyLighting(vec4 color, vec3 normal, float height, bool sideVertex) {
            if (sideVertex) {
                vec3 dimmedColor = color.rgb * (1.0 - 0.5 / (1.0 + height * height));
                vec3 lighting = max(0.0, dot(normal, u_lightDir)) * u_lightColor.rgb + u_ambientColor.rgb;
                return vec4(dimmedColor.rgb * lighting, color.a);
            } else {
                float lighting = max(0.0, dot(normal, u_viewDir)) * 0.5 + 0.5;
                return vec4(color.rgb * lighting, color.a);
            }
        }
    )GLSL";

}
