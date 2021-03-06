/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_PACKAGETILEMASK_H_
#define _CARTO_PACKAGETILEMASK_H_

#include "core/MapPos.h"
#include "core/MapTile.h"

#include <array>
#include <cstdint>
#include <string>
#include <memory>
#include <mutex>
#include <vector>
#include <unordered_set>

namespace carto {
    class Projection;
    class MultiPolygonGeometry;

    namespace PackageTileStatus {
        /**
         * Tile status.
         */
        enum PackageTileStatus {
            /**
             * Tile is not part of the package.
             */
            PACKAGE_TILE_STATUS_MISSING,
            /**
             * Tile is part of the package, but package does not fully cover it.
             * This value is no longer used, thus this is deprecated.
             */
            PACKAGE_TILE_STATUS_PARTIAL,
            /**
             * Tile if part of the package and package fully covers it.
             */
            PACKAGE_TILE_STATUS_FULL
        };
    }

    /**
     * Tile mask contains map package spatial coverage information and
     * can be used for very fast 'tile in package' tests.
     */
    class PackageTileMask {
    public:
        /**
         * Constructs a new package tile mask instance from encoded string and maximum zoom level.
         * @param stringValue The encoded tile mask of the package
         * @param maxZoom The maximum zoom level for the tiles.
         */
        PackageTileMask(const std::string& stringValue, int maxZoom);

        /**
         * Constructs a new package tile mask instance from a list of tiles.
         * @param tiles The list of tiles.
         * @param clipZoom The maximum zoom level to clip the tiles.
         */
        PackageTileMask(const std::vector<MapTile>& tiles, int clipZoom);

        /**
         * Returns the encoded tile mask value. This should not be displayed to the user.
         * @return The tile mask of the package.
         */
        const std::string& getStringValue() const;

        /**
         * Returns the URL-safe tile mask value. This is intended for internal usage.
         * @return The URL-safe tile mask value.
         */
        std::string getURLSafeStringValue() const;
        
        /**
         * Returns maximum zoom level encoded in this tilemask.
         * @return The maximum zoom level encoded in this tilemask.
         */
        int getMaxZoomLevel() const;

        /**
         * Returns the bounding polygon of the tilemask.
         * @param projection The projection to use.
         * @return The bounding polygon of the tilemask area.
         */
        std::shared_ptr<MultiPolygonGeometry> getBoundingPolygon(const std::shared_ptr<Projection>& projection) const;

        /**
         * Returns the status of the specified tile. This method can be used for fast testing whether a tile is part of the package.
         * @param tile The tile to check.
         * @return The status of the specified tile.
         */
        PackageTileStatus::PackageTileStatus getTileStatus(const MapTile& tile) const;

    private:
        struct TileNode {
            std::uint64_t x : 24, y : 24, zoom : 8, inside : 1;
            std::unique_ptr<std::array<TileNode, 4> > subNodes;

            TileNode() : x(0), y(0), zoom(0), inside(1), subNodes() { }
        };

        const TileNode* getRootNode() const;
        const TileNode* findTileNode(const MapTile& tile) const;

        static void BuildTileNode(TileNode& node, const std::unordered_set<MapTile>& tileSet, const MapTile& tile, int clipZoom);
        static void DecodeTileNode(TileNode& node, const std::vector<bool>& data, std::size_t& offset, const MapTile& tile);
        static void EncodeTileNode(const TileNode& node, std::vector<bool>& data);

        static std::vector<std::vector<MapPos> > CalculateTileNodeBoundingPolygon(const TileNode& node, const std::shared_ptr<Projection>& proj);

        std::string _stringValue;
        int _maxZoomLevel;

        mutable std::unique_ptr<TileNode> _cachedRootNode;
        mutable std::mutex _mutex;
    };
}

#endif
