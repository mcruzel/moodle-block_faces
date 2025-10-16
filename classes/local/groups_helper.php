<?php
// This file is part of Moodle - http://moodle.org/
//
// Moodle is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Moodle is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Moodle.  If not, see <http://www.gnu.org/licenses/>.

/**
 * Helper functions for Faces group selection handling.
 *
 * @package   block_faces
 * @copyright 2025 Moodle
 * @license   http://www.gnu.org/copyleft/gpl.html GNU GPL v3 or later
 */

namespace block_faces\local;

defined('MOODLE_INTERNAL') || die();

/**
 * Helper for preparing group selection data shared by renderables.
 */
class groups_helper {
    /**
     * Build the group selection data and selected groups list.
     *
     * @param \stdClass $course The course record.
     * @param \context_course $context The course context.
     * @param array $requestedgroupids Requested group ids from the user interface.
     * @return array An array containing grouping display data and the validated selected groups.
     */
    public static function prepare_group_selection(\stdClass $course, \context_course $context, array $requestedgroupids): array {
        global $CFG;

        require_once($CFG->libdir . '/grouplib.php');

        $selectedgroupids = array_values(array_unique(array_map('intval', $requestedgroupids)));
        $selectedgroups = [];
        foreach ($selectedgroupids as $groupid) {
            if ($groupid <= 0) {
                continue;
            }
            if (!$group = groups_get_group($groupid, '*')) {
                continue;
            }
            if (!groups_group_visible($group, $course, $context)) {
                continue;
            }
            $selectedgroups[$groupid] = $group;
        }

        $groupings = [];
        $usedgroupids = [];
        $coursegroupings = groups_get_all_groupings($course->id);
        if (!empty($coursegroupings)) {
            foreach ($coursegroupings as $grouping) {
                $groupinggroups = groups_get_all_groups($course->id, 0, $grouping->id, 'g.*');
                $groupitems = [];
                foreach ($groupinggroups as $group) {
                    if (!groups_group_visible($group, $course, $context)) {
                        continue;
                    }
                    $usedgroupids[$group->id] = true;
                    $groupitems[] = [
                        'id' => (int)$group->id,
                        'name' => format_string($group->name, true, ['context' => $context]),
                        'checked' => array_key_exists($group->id, $selectedgroups),
                    ];
                }
                if (!empty($groupitems)) {
                    $groupings[] = [
                        'id' => (int)$grouping->id,
                        'name' => format_string($grouping->name, true, ['context' => $context]),
                        'groups' => $groupitems,
                    ];
                }
            }
        }

        $ungroupedgroups = [];
        $allgroups = groups_get_all_groups($course->id, 0, 0, 'g.*');
        foreach ($allgroups as $group) {
            if (isset($usedgroupids[$group->id])) {
                continue;
            }
            if (!groups_group_visible($group, $course, $context)) {
                continue;
            }
            $ungroupedgroups[] = [
                'id' => (int)$group->id,
                'name' => format_string($group->name, true, ['context' => $context]),
                'checked' => array_key_exists($group->id, $selectedgroups),
            ];
        }

        if (!empty($ungroupedgroups)) {
            $groupings[] = [
                'id' => 0,
                'name' => get_string('printgroupsungrouped', 'block_faces'),
                'groups' => $ungroupedgroups,
                'isungrouped' => true,
            ];
        }

        return [
            'groupings' => $groupings,
            'hasgroupings' => !empty($groupings),
            'selectedgroups' => $selectedgroups,
            'selectedgroupids' => array_keys($selectedgroups),
        ];
    }
}
